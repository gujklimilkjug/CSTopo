import csv
import json
import struct
import tempfile
import unittest
from pathlib import Path

from scripts.cstopo_core import (
    CSTopoProject,
    DEFAULT_QGIS_PDAL,
    SURFACE_MANIFEST_SCHEMA_VERSION,
    build_surface_cache,
    build_default_cache_manifest_path,
    build_copc_window_pipeline,
    build_pdal_copc_command,
    compute_runtime_window_sample_spacing,
    create_sample_project,
    create_source_record,
    export_csv,
    export_dxf,
    find_pdal_executable,
    fit_plane_elevation,
    read_las_header,
    synthetic_road_samples,
    _build_surface_tile,
    _build_tin_surface_tile,
    _audit_tin_seams,
    _build_global_tin_tile_data,
    _build_supported_tin_triangles,
    _decimate_tin_points,
    _edge_lengths_xy,
    _extract_tin_boundary_edges,
    _point_key,
)


class CSTopoCoreTests(unittest.TestCase):
    def test_fitted_surface_recovers_plane_elevation(self):
        samples = synthetic_road_samples(5000.0, 1000.0)
        elevation, residual = fit_plane_elevation(samples, 5000.0, 1000.0)
        self.assertAlmostEqual(elevation, 165.0, places=6)
        self.assertAlmostEqual(residual, 0.0, places=6)

    def test_add_shots_create_code_figures_and_numbering(self):
        project = CSTopoProject.default("Unit Test")
        project.add_fitted_shot(5000.0, 1000.0, synthetic_road_samples(5000.0, 1000.0), code="EOP")
        project.add_fitted_shot(5025.0, 1000.0, synthetic_road_samples(5025.0, 1000.0), code="EOP")
        project.add_fitted_shot(5000.0, 1012.0, synthetic_road_samples(5000.0, 1012.0), code="CL")

        self.assertEqual([shot.point_number for shot in project.shots], [1, 2, 3])
        self.assertEqual(len(project.figures), 2)
        self.assertEqual(project.figures[0].point_numbers, [1, 2])
        self.assertEqual(project.figures[1].point_numbers, [3])

    def test_save_load_and_exports(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            project = create_sample_project()
            project_path = root / "demo.cstopo"
            csv_path = root / "demo.csv"
            dxf_path = root / "demo.dxf"

            project.save(project_path)
            loaded = CSTopoProject.load(project_path)
            export_csv(loaded, csv_path)
            export_dxf(loaded, dxf_path)

            manifest_path = build_default_cache_manifest_path(project_path)
            self.assertTrue(manifest_path.exists())
            self.assertTrue(loaded.cache_manifest_path.endswith(".cachemanifest.json"))

            self.assertEqual(len(loaded.shots), 8)
            with csv_path.open(newline="", encoding="utf-8") as handle:
                rows = list(csv.DictReader(handle))
            self.assertEqual(rows[0]["Code"], "EOP")
            self.assertEqual(rows[-1]["Code"], "CL")

            dxf = dxf_path.read_text(encoding="utf-8")
            self.assertIn("POLYLINE", dxf)
            self.assertIn("POINT", dxf)
            self.assertIn("EOP", dxf)
            self.assertIn("CL", dxf)

    def test_import_record_and_pdal_command(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "survey.las"
            cache = root / "cache"
            write_minimal_las_header(source)

            header = read_las_header(source)
            self.assertEqual(header.version, "1.2")
            self.assertEqual(header.point_count, 1234)
            self.assertEqual(header.bounds_min, (100.0, 200.0, 10.0))
            self.assertEqual(header.bounds_max, (150.0, 260.0, 25.0))

            record = create_source_record(source, cache)
            self.assertEqual(record.cache_format, "COPC")
            self.assertEqual(Path(record.cache_path), cache / "survey.copc.laz")
            self.assertEqual(record.point_count, 1234)
            self.assertFalse(record.cache_exists)
            self.assertEqual(record.runtime_display_path, str(source))
            self.assertIn("Runtime path", record.cache_status)

            command = build_pdal_copc_command(source, cache)
            self.assertTrue(command[0].endswith("pdal") or command[0].endswith("pdal.exe"))
            self.assertEqual(command[1], "translate")
            self.assertEqual(command[-2:], ["--writer", "writers.copc"])

    def test_project_save_load_preserves_cache_manifest_state(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "survey.las"
            cache = root / "cache"
            write_minimal_las_header(source)

            project = CSTopoProject.default("Cache Manifest")
            record = create_source_record(source, cache)
            record.is_active = True
            project.active_point_cloud_id = record.source_id
            project.point_clouds.append(record)

            project_path = root / "cache_manifest_demo.cstopo"
            project.save(project_path)
            loaded = CSTopoProject.load(project_path)

            self.assertEqual(len(loaded.point_clouds), 1)
            self.assertTrue(loaded.cache_manifest_path.endswith(".cachemanifest.json"))
            self.assertEqual(loaded.point_clouds[0].cache_state, "Missing")
            self.assertEqual(loaded.point_clouds[0].runtime_display_path, str(source))

    def test_find_pdal_prefers_configured_path(self):
        with tempfile.TemporaryDirectory() as temp:
            fake_pdal = Path(temp) / "pdal.exe"
            fake_pdal.write_text("", encoding="utf-8")
            old_default = DEFAULT_QGIS_PDAL
            self.assertTrue(old_default)
            import os

            previous = os.environ.get("CSTOPO_PDAL_PATH")
            os.environ["CSTOPO_PDAL_PATH"] = str(fake_pdal)
            try:
                self.assertEqual(find_pdal_executable(), str(fake_pdal))
            finally:
                if previous is None:
                    os.environ.pop("CSTOPO_PDAL_PATH", None)
                else:
                    os.environ["CSTOPO_PDAL_PATH"] = previous

    def test_runtime_window_pipeline_uses_budgeted_spacing_and_bounds(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "survey.las"
            cache = root / "cache"
            write_minimal_las_header(source)

            record = create_source_record(source, cache)
            spacing = compute_runtime_window_sample_spacing(350.0, 750000)
            self.assertGreaterEqual(spacing, 0.15)

            pipeline = build_copc_window_pipeline(
                record,
                cache / "runtime" / "window.las",
                120.0,
                230.0,
                350.0,
                spacing,
                25.0,
            )
            self.assertEqual(pipeline[0]["type"], "readers.copc")
            self.assertIn("([", pipeline[0]["bounds"])
            self.assertEqual(pipeline[1]["type"], "filters.sample")
            self.assertEqual(pipeline[2]["type"], "writers.las")

    def test_surface_cache_build_creates_manifest_and_tiles(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "surface_source.las"
            cache = root / "cache"
            write_surface_test_las(source)

            project = CSTopoProject.default("Surface Test")
            record = create_source_record(source, cache)
            record.is_active = True
            project.active_point_cloud_id = record.source_id
            project.point_clouds.append(record)

            manifest = build_surface_cache(project, record.source_id, cache, force_rebuild=True)

            self.assertEqual(manifest.build_state, "Ready")
            self.assertEqual(manifest.schema_version, SURFACE_MANIFEST_SCHEMA_VERSION)
            self.assertEqual(manifest.tin_boundary_mode, "SupportBoundary")
            self.assertGreater(manifest.tin_resolved_max_edge_length_source_units, 0.0)
            self.assertGreater(manifest.tile_count, 0)
            self.assertTrue(Path(manifest.manifest_path).exists())
            self.assertTrue(project.point_clouds[0].surface_manifest_path.endswith("surface_manifest.json"))
            self.assertEqual(project.point_clouds[0].surface_build_state, "Ready")
            self.assertGreater(project.derived_surfaces[0].tiles[0].triangle_count, 0)

    def test_surface_cache_build_writes_progress_file(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "surface_source.las"
            cache = root / "cache"
            progress_path = cache / "surfaces" / "progress.json"
            write_surface_test_las(source)

            project = CSTopoProject.default("Surface Progress Test")
            record = create_source_record(source, cache)
            record.is_active = True
            project.active_point_cloud_id = record.source_id
            project.point_clouds.append(record)

            build_surface_cache(project, record.source_id, cache, force_rebuild=True, progress_path=progress_path)

            progress = json.loads(progress_path.read_text(encoding="utf-8"))
            self.assertEqual(progress["stage"], "Ready")
            self.assertEqual(progress["progress"], 1.0)
            self.assertIn("Surface ready", progress["message"])

    def test_surface_tile_does_not_bridge_unsupported_voids(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            tile = _build_surface_tile(
                "void_guard",
                [
                    (0.0, 0.0, 10.0),
                    (10.0, 0.0, 10.0),
                    (0.0, 10.0, 10.0),
                    (10.0, 10.0, 10.0),
                ],
                0.0,
                0.0,
                10.0,
                10.0,
                0.0,
                0.0,
                10.0,
                10.0,
                1.0,
                root / "void_guard.json",
                max_fill_cells=1,
                max_triangle_z_delta=6.0,
            )

            self.assertIsNone(tile)

    def test_surface_tiles_clip_padding_to_non_overlapping_core_bounds(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            points = [
                (float(x), float(y), 10.0 + (x * 0.1) + (y * 0.05))
                for x in range(0, 21)
                for y in range(0, 11)
            ]

            left = _build_surface_tile(
                "left",
                points,
                0.0,
                0.0,
                12.0,
                10.0,
                0.0,
                0.0,
                10.0,
                10.0,
                1.0,
                root / "left.json",
            )
            right = _build_surface_tile(
                "right",
                points,
                8.0,
                0.0,
                20.0,
                10.0,
                10.0,
                0.0,
                20.0,
                10.0,
                1.0,
                root / "right.json",
            )

            self.assertIsNotNone(left)
            self.assertIsNotNone(right)
            overlap_x = min(left.bounds_max[0], right.bounds_max[0]) - max(left.bounds_min[0], right.bounds_min[0])
            overlap_y = min(left.bounds_max[1], right.bounds_max[1]) - max(left.bounds_min[1], right.bounds_min[1])
            self.assertLessEqual(overlap_x, 0.0)
            self.assertGreater(overlap_y, 0.0)

    def test_surface_tile_uses_padding_for_edge_support_without_exporting_padding(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            tile = _build_surface_tile(
                "padded",
                [
                    (float(x), float(y), 10.0)
                    for x in range(-1, 4)
                    for y in range(-1, 4)
                ],
                -1.0,
                -1.0,
                3.0,
                3.0,
                0.0,
                0.0,
                2.0,
                2.0,
                1.0,
                root / "padded.json",
                max_fill_cells=2,
                max_triangle_z_delta=6.0,
            )

            self.assertIsNotNone(tile)
            self.assertGreater(tile.triangle_count, 0)
            self.assertGreaterEqual(tile.bounds_min[0], 0.0)
            self.assertGreaterEqual(tile.bounds_min[1], 0.0)
            self.assertLessEqual(tile.bounds_max[0], 2.0)
            self.assertLessEqual(tile.bounds_max[1], 2.0)

    def test_tin_tile_builds_from_irregular_points(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            points = [
                (0.0, 0.0, 10.0),
                (3.0, 0.4, 10.4),
                (7.0, 0.1, 10.8),
                (10.0, 0.0, 11.0),
                (0.2, 5.0, 10.3),
                (4.8, 5.3, 10.8),
                (10.0, 4.9, 11.4),
                (0.0, 10.0, 10.6),
                (5.2, 10.0, 11.1),
                (10.0, 10.0, 11.8),
            ]
            exported_keys = set()
            exported_triangles = []

            tile, duplicates = _build_tin_surface_tile(
                "tin",
                0,
                0,
                points,
                0.0,
                0.0,
                10.0,
                10.0,
                10.0,
                0,
                0,
                root / "tin.json",
                max_edge_length=20.0,
                max_triangle_z_delta=None,
                max_buffered_points=150000,
                decimation_mode="PreserveExtremesAdaptive",
                exported_triangle_keys=exported_keys,
                exported_triangles=exported_triangles,
            )

            self.assertEqual(duplicates, 0)
            self.assertIsNotNone(tile)
            self.assertGreater(tile.triangle_count, 0)
            self.assertEqual(tile.decimation_method, "none")
            self.assertEqual(tile.input_point_count, tile.used_point_count)
            self.assertTrue((root / "tin.json").exists())

    def test_tin_support_boundary_rejects_sparse_exterior_triangles(self):
        points = [
            (float(x), float(y), 10.0 + x * 0.1 + y * 0.05)
            for x in range(0, 5)
            for y in range(0, 5)
        ] + [
            (25.0, 0.0, 12.5),
            (25.0, 4.0, 12.7),
        ]

        triangles, diagnostics = _build_supported_tin_triangles(
            points,
            0.0,
            0.0,
            25.0,
            4.0,
            30.0,
            0,
            0,
            configured_edge_length=2.25,
            edge_multiplier=8.0,
            area_multiplier=0.75,
            max_triangle_z_delta=None,
            constrain_to_boundary=True,
        )

        self.assertGreater(len(triangles), 0)
        self.assertGreater(diagnostics.rejected_large_triangle_count, 0)
        for triangle in triangles:
            self.assertLessEqual(max(_edge_lengths_xy(triangle)), 2.25)

    def test_tin_support_boundary_does_not_bridge_concave_void(self):
        points = []
        for x in range(0, 11):
            for y in range(0, 3):
                points.append((float(x), float(y), 100.0 + x * 0.1 + y * 0.01))
        for x in list(range(0, 3)) + list(range(8, 11)):
            for y in range(3, 8):
                points.append((float(x), float(y), 100.0 + x * 0.1 + y * 0.01))

        triangles, diagnostics = _build_supported_tin_triangles(
            points,
            0.0,
            0.0,
            10.0,
            7.0,
            20.0,
            0,
            0,
            configured_edge_length=2.25,
            edge_multiplier=8.0,
            area_multiplier=0.75,
            max_triangle_z_delta=None,
            constrain_to_boundary=True,
        )

        self.assertGreater(len(triangles), 0)
        self.assertGreater(diagnostics.rejected_large_triangle_count, 0)
        for triangle in triangles:
            centroid_x = sum(point[0] for point in triangle) / 3.0
            centroid_y = sum(point[1] for point in triangle) / 3.0
            self.assertFalse(3.0 < centroid_x < 7.0 and centroid_y > 3.0)

    def test_tin_boundary_extraction_finds_closed_support_loop(self):
        points = [
            (float(x), float(y), 50.0)
            for x in range(0, 4)
            for y in range(0, 4)
        ]

        triangles, diagnostics = _build_supported_tin_triangles(
            points,
            0.0,
            0.0,
            3.0,
            3.0,
            5.0,
            0,
            0,
            configured_edge_length=2.0,
            edge_multiplier=8.0,
            area_multiplier=0.75,
            max_triangle_z_delta=None,
            constrain_to_boundary=True,
        )

        boundary_edges = _extract_tin_boundary_edges(triangles)
        self.assertGreater(len(boundary_edges), 0)
        self.assertEqual(diagnostics.boundary_loop_count, 1)
        self.assertEqual(diagnostics.constrained_triangulation_status, "Constrained")

    def test_global_tin_partition_uses_shared_polygon_edges_between_tiles(self):
        points = [
            (float(x), float(y), 10.0 + x * 0.1 + y * 0.05)
            for x in range(0, 21, 2)
            for y in range(0, 11, 2)
        ]

        triangles_by_owner, boundary_edges_by_owner, diagnostics, status, message = _build_global_tin_tile_data(
            points,
            0.0,
            0.0,
            20.0,
            10.0,
            10.0,
            1,
            0,
            configured_edge_length=8.0,
            edge_multiplier=8.0,
            area_multiplier=0.75,
            max_triangle_z_delta=None,
        )

        self.assertEqual(status, "Passed", message)
        self.assertIn((0, 0), triangles_by_owner)
        self.assertIn((1, 0), triangles_by_owner)
        left_edges = set(boundary_edges_by_owner[(0, 0)])
        right_edges = set(boundary_edges_by_owner[(1, 0)])
        shared_edges = left_edges.intersection(right_edges)
        self.assertGreater(len(shared_edges), 0)
        self.assertGreater(diagnostics.boundary_loop_count, 0)

    def test_global_tin_partition_has_single_triangle_ownership(self):
        points = [
            (float(x), float(y), 20.0 + x * 0.03 + y * 0.02)
            for x in range(0, 16)
            for y in range(0, 8)
        ]

        triangles_by_owner, _boundary_edges_by_owner, diagnostics, status, message = _build_global_tin_tile_data(
            points,
            0.0,
            0.0,
            15.0,
            7.0,
            5.0,
            2,
            1,
            configured_edge_length=3.0,
            edge_multiplier=8.0,
            area_multiplier=0.75,
            max_triangle_z_delta=None,
        )

        self.assertEqual(status, "Passed", message)
        triangle_keys = []
        for triangles in triangles_by_owner.values():
            triangle_keys.extend(tuple(sorted(_point_key(point) for point in triangle)) for triangle in triangles)
        self.assertEqual(len(triangle_keys), len(set(triangle_keys)))
        self.assertEqual(diagnostics.retained_triangle_count, len(triangle_keys))

    def test_global_tin_partition_boundary_is_not_square_clipped(self):
        points = [
            (0.0, 0.0, 10.0),
            (4.0, 0.0, 10.4),
            (8.0, 0.0, 10.8),
            (0.0, 4.0, 10.2),
            (4.0, 4.0, 10.6),
            (8.0, 4.0, 11.0),
            (2.0, 8.0, 10.6),
            (6.0, 8.0, 11.0),
        ]

        triangles_by_owner, boundary_edges_by_owner, _diagnostics, status, message = _build_global_tin_tile_data(
            points,
            0.0,
            0.0,
            8.0,
            8.0,
            4.0,
            1,
            1,
            configured_edge_length=6.0,
            edge_multiplier=8.0,
            area_multiplier=0.75,
            max_triangle_z_delta=None,
        )

        self.assertEqual(status, "Passed", message)
        all_boundary_keys = {key for edges in boundary_edges_by_owner.values() for edge in edges for key in edge}
        self.assertIn(_point_key((2.0, 8.0, 10.6)), all_boundary_keys)
        self.assertIn(_point_key((6.0, 8.0, 11.0)), all_boundary_keys)
        for triangles in triangles_by_owner.values():
            for triangle in triangles:
                for point in triangle:
                    self.assertIn(point, points)

    def test_neighboring_tin_tiles_have_single_triangle_ownership_and_pass_seam_audit(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            points = [
                (float(x), float(y), 10.0 + (x * 0.05) + (y * 0.02))
                for x in range(0, 21, 2)
                for y in range(0, 11, 2)
            ]
            exported_keys = set()
            exported_triangles = []

            left_points = [point for point in points if 0.0 <= point[0] <= 12.0]
            right_points = [point for point in points if 8.0 <= point[0] <= 20.0]
            left, left_duplicates = _build_tin_surface_tile(
                "left",
                0,
                0,
                left_points,
                0.0,
                0.0,
                20.0,
                10.0,
                10.0,
                1,
                0,
                root / "left.json",
                max_edge_length=8.0,
                max_triangle_z_delta=None,
                max_buffered_points=150000,
                decimation_mode="PreserveExtremesAdaptive",
                exported_triangle_keys=exported_keys,
                exported_triangles=exported_triangles,
            )
            right, right_duplicates = _build_tin_surface_tile(
                "right",
                1,
                0,
                right_points,
                0.0,
                0.0,
                20.0,
                10.0,
                10.0,
                1,
                0,
                root / "right.json",
                max_edge_length=8.0,
                max_triangle_z_delta=None,
                max_buffered_points=150000,
                decimation_mode="PreserveExtremesAdaptive",
                exported_triangle_keys=exported_keys,
                exported_triangles=exported_triangles,
            )

            self.assertIsNotNone(left)
            self.assertIsNotNone(right)
            self.assertEqual(left_duplicates + right_duplicates, 0)
            self.assertEqual(len(exported_keys), left.triangle_count + right.triangle_count)

            status, message = _audit_tin_seams(
                exported_triangles,
                {(0, 0), (1, 0)},
                0.0,
                0.0,
                20.0,
                10.0,
                10.0,
                1,
                0,
                0,
                8.0,
            )
            self.assertEqual(status, "Passed", message)

    def test_tin_decimation_does_not_run_below_cap(self):
        points = [(float(index), 0.0, float(index % 3)) for index in range(10)]
        used, method = _decimate_tin_points(points, 20, "PreserveExtremesAdaptive")

        self.assertEqual(method, "none")
        self.assertEqual(used, points)

    def test_tin_decimation_preserves_elevation_extremes_when_forced(self):
        points = [
            (0.01 * index, 0.01 * index, 50.0)
            for index in range(8)
        ] + [
            (0.04, 0.02, 1.0),
            (0.05, 0.03, 100.0),
        ]
        used, method = _decimate_tin_points(points, 6, "PreserveExtremesAdaptive")
        used_keys = {_point_key(point) for point in used}

        self.assertNotEqual(method, "none")
        self.assertIn(_point_key((0.04, 0.02, 1.0)), used_keys)
        self.assertIn(_point_key((0.05, 0.03, 100.0)), used_keys)


def write_minimal_las_header(path: Path) -> None:
    data = bytearray(411)
    data[0:4] = b"LASF"
    data[24] = 1
    data[25] = 2
    struct.pack_into("<H", data, 94, 227)
    struct.pack_into("<I", data, 96, 411)
    struct.pack_into("<I", data, 100, 1)
    data[104] = 2
    struct.pack_into("<H", data, 105, 26)
    struct.pack_into("<I", data, 107, 1234)
    struct.pack_into("<ddd", data, 131, 0.01, 0.01, 0.01)
    struct.pack_into("<ddd", data, 155, 100.0, 200.0, 10.0)
    struct.pack_into("<dddddd", data, 179, 150.0, 100.0, 260.0, 200.0, 25.0, 10.0)
    path.write_bytes(data)


def write_surface_test_las(path: Path) -> None:
    points = [
        (100.0, 200.0, 10.0),
        (101.0, 200.0, 10.2),
        (102.0, 200.0, 10.4),
        (100.0, 201.0, 10.3),
        (101.0, 201.0, 10.5),
        (102.0, 201.0, 10.7),
        (100.0, 202.0, 10.6),
        (101.0, 202.0, 10.8),
        (102.0, 202.0, 11.0),
    ]

    scale = (0.01, 0.01, 0.01)
    offset = (0.0, 0.0, 0.0)
    point_format = 2
    point_record_length = 26
    header_size = 227
    point_data_offset = header_size
    data = bytearray(point_data_offset + len(points) * point_record_length)
    data[0:4] = b"LASF"
    data[24] = 1
    data[25] = 2
    struct.pack_into("<H", data, 94, header_size)
    struct.pack_into("<I", data, 96, point_data_offset)
    struct.pack_into("<I", data, 100, 0)
    data[104] = point_format
    struct.pack_into("<H", data, 105, point_record_length)
    struct.pack_into("<I", data, 107, len(points))
    struct.pack_into("<ddd", data, 131, *scale)
    struct.pack_into("<ddd", data, 155, *offset)
    struct.pack_into("<dddddd", data, 179, 102.0, 100.0, 202.0, 200.0, 11.0, 10.0)

    cursor = point_data_offset
    for x, y, z in points:
        ix = int(round((x - offset[0]) / scale[0]))
        iy = int(round((y - offset[1]) / scale[1]))
        iz = int(round((z - offset[2]) / scale[2]))
        struct.pack_into("<iii", data, cursor, ix, iy, iz)
        struct.pack_into("<H", data, cursor + 12, 1000)
        data[cursor + 15] = 2
        cursor += point_record_length

    path.write_bytes(data)


if __name__ == "__main__":
    unittest.main()
