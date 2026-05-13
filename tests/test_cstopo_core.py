import csv
import json
import math
import struct
import tempfile
import unittest
from unittest import mock
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
    load_control_code_definitions,
    measurement_display_code,
    project_from_dict,
    parse_control_command,
    read_las_header,
    synthetic_road_samples,
    _build_surface_tile,
    _build_tin_surface_tile,
    _audit_tin_seams,
    _boundary_edge_count_for_triangle_indices,
    _build_global_tin_array_tile_data,
    _build_global_tin_tile_data,
    _build_supported_tin_triangles,
    _decimate_tin_points,
    _dedupe_xy_points,
    _dedupe_xy_points_numpy,
    _edge_lengths_xy,
    _extract_tin_boundary_edges,
    _estimate_global_tin_memory_megabytes,
    _filter_tin_triangle_indices_numpy,
    _filter_tin_triangles_numpy,
    _filter_tin_triangles_python,
    _nonmanifold_edge_count_for_triangle_indices,
    _packed_undirected_edge_keys,
    _packed_xy_keys_from_array,
    _plan_tin_audit_memory,
    _point_key,
    _read_las_xyz_points_numpy,
    _read_las_xyz_points_python,
    _read_las_xyz_array_numpy,
    _structured_triangle_keys,
    _unique_triangle_first_indices_and_duplicate_count,
    _resolve_surface_build_worker_count,
    _read_cstin_binary_tile,
    _write_cstin_binary_tile,
    _write_global_tin_surface_tiles,
    _write_global_tin_binary_surface_tiles,
    write_surface_build_progress,
)


class CSTopoCoreTests(unittest.TestCase):
    def test_fitted_surface_recovers_plane_elevation(self):
        samples = synthetic_road_samples(5000.0, 1000.0)
        elevation, residual = fit_plane_elevation(samples, 5000.0, 1000.0)
        self.assertAlmostEqual(elevation, 165.0, places=6)
        self.assertAlmostEqual(residual, 0.0, places=6)

    def test_add_shots_create_code_figures_and_numbering(self):
        project = CSTopoProject.default("Unit Test")
        project.add_fitted_shot(5000.0, 1000.0, synthetic_road_samples(5000.0, 1000.0), code="BLDPAD")
        project.add_fitted_shot(5025.0, 1000.0, synthetic_road_samples(5025.0, 1000.0), code="BLDPAD")
        project.add_fitted_shot(5000.0, 1012.0, synthetic_road_samples(5000.0, 1012.0), code="DECK")

        self.assertEqual([shot.point_number for shot in project.shots], [1, 2, 3])
        self.assertEqual(len(project.figures), 2)
        self.assertEqual(project.figures[0].point_numbers, [1, 2])
        self.assertEqual(project.figures[1].point_numbers, [3])

    def test_default_code_palette_uses_embedded_code_list(self):
        project = CSTopoProject.default("Codes")
        self.assertGreaterEqual(len(project.code_palette), 323)

        by_code = {style.code: style for style in project.code_palette}
        self.assertIn("AXLE", by_code)
        self.assertEqual(by_code["AXLE"].category, "MONUMENT")
        self.assertEqual(by_code["AXLE"].display_name, "Axle")
        self.assertEqual(by_code["AXLE"].point_type, "Point (no triangulation)")
        self.assertTrue(by_code["AXLE"].color.startswith("#"))

    def test_loaded_project_palette_is_pruned_to_code_list(self):
        project = project_from_dict(
            {
                "schema_version": "1.3",
                "project_name": "Legacy Codes",
                "active_code": "CL",
                "next_point_number": 1,
                "code_palette": [
                    {"code": "EOP", "layer_name": "EOP", "color": "yellow"},
                    {"code": "CL", "layer_name": "CL", "color": "cyan"},
                    {"code": "FL", "layer_name": "FL", "color": "green"},
                    {"code": "GB", "layer_name": "GB", "color": "red"},
                ],
                "figures": [
                    {
                        "figure_id": "legacy",
                        "code": "CL",
                        "layer_name": "CL",
                        "point_numbers": [],
                        "closed": False,
                        "style": {"code": "CL", "layer_name": "CL"},
                    }
                ],
            }
        )

        codes = {style.code for style in project.code_palette}
        self.assertNotIn("EOP", codes)
        self.assertNotIn("CL", codes)
        self.assertNotIn("GB", codes)
        self.assertIn("EO", codes)
        self.assertIn("FL", codes)
        self.assertEqual(project.active_code, "AXLE")
        self.assertEqual(project.figures[0].style.point_type, "Point (no triangulation)")

    def test_point_types_control_figures_and_dxf_linework(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            project = CSTopoProject.default("Point Types")
            project.add_fitted_shot(5000.0, 1000.0, synthetic_road_samples(5000.0, 1000.0), code="AXLE")
            project.add_fitted_shot(5010.0, 1000.0, synthetic_road_samples(5010.0, 1000.0), code="AXLE")
            project.add_fitted_shot(5000.0, 1010.0, synthetic_road_samples(5000.0, 1010.0), code="PEND")
            project.add_fitted_shot(5010.0, 1010.0, synthetic_road_samples(5010.0, 1010.0), code="PEND")
            self.assertEqual(project.figures, [])
            self.assertEqual([shot.figure_id for shot in project.shots], ["", "", "", ""])

            legacy_point_figure = project.open_figure_for("PEND")
            legacy_point_figure.point_numbers.extend([3, 4])
            project.add_fitted_shot(5000.0, 1020.0, synthetic_road_samples(5000.0, 1020.0), code="DECK")
            project.add_fitted_shot(5010.0, 1020.0, synthetic_road_samples(5010.0, 1020.0), code="DECK")
            project.add_fitted_shot(5000.0, 1030.0, synthetic_road_samples(5000.0, 1030.0), code="BLDPAD")
            project.add_fitted_shot(5010.0, 1030.0, synthetic_road_samples(5010.0, 1030.0), code="BLDPAD")

            linework_figures = [figure for figure in project.figures if figure.code in {"DECK", "BLDPAD"}]
            self.assertEqual(len(linework_figures), 2)
            self.assertEqual(linework_figures[0].point_numbers, [5, 6])
            self.assertEqual(linework_figures[1].point_numbers, [7, 8])

            dxf_path = root / "point_types.dxf"
            export_dxf(project, dxf_path)
            dxf = dxf_path.read_text(encoding="utf-8")
            self.assertEqual(dxf.count("\nPOINT\n"), 8)
            self.assertEqual(dxf.count("\nPOLYLINE\n"), 2)
            self.assertIn("\n8\nDECK\n", dxf)
            self.assertIn("\n8\nBLDPAD\n", dxf)

    def test_control_code_list_and_command_parsing(self):
        controls = load_control_code_definitions()
        codes = [control.code for control in controls]
        self.assertEqual(
            codes,
            ["CLS", "END", "ESC", "IG", "JPT", "NPC", "NPT", "OH", "OV", "PC", "RECT", "SCE", "SCR", "SSC", "ST"],
        )

        self.assertEqual(parse_control_command("TOEFILL").base_code, "TOEFILL")
        parsed = parse_control_command("TOEFILL OH 2.5")
        self.assertEqual((parsed.base_code, parsed.control_code, parsed.parameter), ("TOEFILL", "OH", "2.5"))
        parsed = parse_control_command("TOEFILL ST OH 2.5")
        self.assertEqual((parsed.base_code, parsed.control_code, parsed.parameter, parsed.starts_new_figure), ("TOEFILL", "OH", "2.5", True))
        parsed = parse_control_command("JPT 42", active_code="TOEFILL")
        self.assertEqual((parsed.base_code, parsed.control_code, parsed.parameter), ("TOEFILL", "JPT", "42"))
        parsed = parse_control_command("TOEFILL RECT")
        self.assertEqual((parsed.base_code, parsed.control_code, parsed.parameter), ("TOEFILL", "RECT", ""))
        parsed = parse_control_command("TOEFILL PT")
        self.assertEqual((parsed.base_code, parsed.control_code, parsed.parameter), ("TOEFILL", "", ""))
        parsed = parse_control_command("TOEFILL SCPT")
        self.assertEqual((parsed.base_code, parsed.control_code, parsed.parameter), ("TOEFILL", "", ""))
        self.assertEqual(measurement_display_code("toefill", "st"), "TOEFILL ST")

    def test_control_codes_are_one_shot_and_update_figures(self):
        project = CSTopoProject.default("Controls")
        project.set_pending_control_code("ST")
        first = project.add_shot(0.0, 0.0, 100.0, code="BLDPAD")
        second = project.add_shot(10.0, 0.0, 100.0, code="BLDPAD")
        project.set_pending_control_code("IG")
        ignored = project.add_shot(20.0, 0.0, 100.0, code="BLDPAD")
        project.set_pending_control_code("CLS")
        closed = project.add_shot(10.0, 10.0, 100.0, code="BLDPAD")

        self.assertEqual(first.code, "BLDPAD ST")
        self.assertEqual(second.code, "BLDPAD")
        self.assertEqual(ignored.code, "BLDPAD IG")
        self.assertEqual(project.pending_control_code, "")
        self.assertEqual(project.figures[0].point_numbers, [1, 2, 4])
        self.assertTrue(project.figures[0].loop_closed)
        self.assertGreaterEqual(len(project.figure_segments), 3)

        project.set_pending_control_code("END")
        ended = project.add_shot(30.0, 0.0, 100.0, code="BLDPAD")
        self.assertEqual(ended.code, "BLDPAD END")
        self.assertTrue(project.figures[-1].closed)

    def test_control_code_parameters_geometry_and_undo(self):
        project = CSTopoProject.default("Geometry Controls")
        project.add_shot(0.0, 0.0, 100.0, code="BLDPAD ST")
        project.add_shot(10.0, 0.0, 100.0, code="BLDPAD")
        project.add_shot(10.0, 10.0, 100.0, code="BLDPAD JPT 1")
        project.add_shot(20.0, 10.0, 100.0, code="BLDPAD ST OH 2.5")
        project.add_shot(25.0, 10.0, 100.0, code="BLDPAD OH 2.5")
        project.add_shot(30.0, 10.0, 100.0, code="BLDPAD RECT")
        project.add_shot(34.0, 10.0, 100.0, code="BLDPAD")
        project.add_shot(34.0, 14.0, 100.0, code="BLDPAD")
        project.add_shot(35.0, 15.0, 100.0, code="BLDPAD SCE")
        project.add_shot(40.0, 15.0, 100.0, code="BLDPAD SCR 6.0")
        project.add_shot(45.0, 15.0, 100.0, code="BLDPAD SSC")
        project.add_shot(47.0, 17.0, 100.0, code="BLDPAD")
        project.add_shot(49.0, 15.0, 100.0, code="BLDPAD ESC")

        kinds = {segment.segment_kind for segment in project.figure_segments}
        self.assertIn("JoinLine", kinds)
        self.assertIn("OffsetLine", kinds)
        self.assertIn("Rectangle", kinds)
        self.assertIn("Circle", kinds)
        self.assertIn("SmoothCurve", kinds)

        with self.assertRaises(ValueError):
            project.add_shot(50.0, 15.0, 100.0, code="BLDPAD OH")

        self.assertTrue(project.undo_last_measurement())
        self.assertEqual(project.next_point_number, 13)
        self.assertIn("SmoothCurve", {segment.segment_kind for segment in project.figure_segments})

    def test_pc_automatically_applies_pt_to_next_shot_and_draws_arc(self):
        project = CSTopoProject.default("Tangent Arc")
        project.add_shot(0.0, 0.0, 100.0, code="BLDPAD ST")
        project.add_shot(10.0, 0.0, 100.0, code="BLDPAD")
        project.set_pending_control_code("PC")
        pc = project.add_shot(20.0, 0.0, 100.0, code="BLDPAD")
        self.assertEqual(pc.code, "BLDPAD PC")
        self.assertEqual(project.pending_control_code, "PT")
        pt = project.add_shot(30.0, 10.0, 100.0, code="BLDPAD")
        self.assertEqual(pt.code, "BLDPAD PT")
        self.assertEqual(project.pending_control_code, "")

        arcs = [segment for segment in project.figure_segments if segment.segment_kind == "Arc"]
        self.assertEqual(len(arcs), 1)
        self.assertEqual(arcs[0].point_numbers, [pc.point_number, pt.point_number])
        self.assertAlmostEqual(arcs[0].survey_points[0][0], pc.northing)
        self.assertAlmostEqual(arcs[0].survey_points[0][1], pc.easting)
        self.assertAlmostEqual(arcs[0].survey_points[-1][0], pt.northing)
        self.assertAlmostEqual(arcs[0].survey_points[-1][1], pt.easting)
        self.assertNotIn([pc.point_number, pt.point_number], [segment.point_numbers for segment in project.figure_segments if segment.segment_kind == "Line"])

    def test_curve_linework_uses_tenth_foot_sampling_metric(self):
        def assert_tenth_foot_steps(points):
            for previous, point in zip(points, points[1:]):
                horizontal = math.hypot(point[0] - previous[0], point[1] - previous[1])
                vertical = abs(point[2] - previous[2])
                self.assertLessEqual(horizontal, 0.100001)
                self.assertLessEqual(vertical, 0.100001)

        project = CSTopoProject.default("Metric Arc")
        project.add_shot(0.0, 0.0, 100.0, code="BLDPAD ST")
        project.add_shot(10.0, 0.0, 100.0, code="BLDPAD")
        project.set_pending_control_code("PC")
        project.add_shot(20.0, 0.0, 100.0, code="BLDPAD")
        project.add_shot(30.0, 10.0, 105.0, code="BLDPAD")
        arcs = [segment for segment in project.figure_segments if segment.segment_kind == "Arc"]
        self.assertEqual(len(arcs), 1)
        self.assertGreater(len(arcs[0].survey_points), 49)
        assert_tenth_foot_steps(arcs[0].survey_points)

        project = CSTopoProject.default("Metric Smooth Curve")
        project.add_shot(0.0, 0.0, 100.0, code="BLDPAD ST")
        project.add_shot(10.0, 0.0, 100.0, code="BLDPAD SSC")
        project.add_shot(15.0, 4.0, 103.0, code="BLDPAD")
        project.add_shot(20.0, 0.0, 106.0, code="BLDPAD ESC")
        curves = [segment for segment in project.figure_segments if segment.segment_kind == "SmoothCurve"]
        self.assertEqual(len(curves), 1)
        assert_tenth_foot_steps(curves[0].survey_points)

    def test_oh_persists_until_cancel_and_suppresses_measured_line(self):
        project = CSTopoProject.default("Persistent Offset")
        project.add_shot(0.0, 0.0, 100.0, code="BLDPAD ST")
        project.add_shot(10.0, 0.0, 100.0, code="BLDPAD")

        project.set_pending_control_code("OH", "2.5")
        offset_start = project.add_shot(10.0, 10.0, 100.0, code="BLDPAD")
        offset_end = project.add_shot(20.0, 10.0, 100.0, code="BLDPAD")
        offset_bend_end = project.add_shot(20.0, 20.0, 101.0, code="BLDPAD")
        project.cancel_pending_control_code()
        normal_start = project.add_shot(30.0, 10.0, 100.0, code="BLDPAD")
        normal_end = project.add_shot(40.0, 10.0, 100.0, code="BLDPAD")

        self.assertEqual(offset_start.code, "BLDPAD ST OH 2.5")
        self.assertEqual(offset_end.code, "BLDPAD OH 2.5")
        self.assertTrue(offset_start.starts_new_figure)
        self.assertEqual(project.pending_control_code, "")
        self.assertEqual(normal_start.code, "BLDPAD ST")

        offset_segments = [segment for segment in project.figure_segments if segment.segment_kind == "OffsetLine"]
        self.assertEqual(len(offset_segments), 1)
        self.assertEqual(offset_segments[0].point_numbers, [offset_start.point_number, offset_end.point_number, offset_bend_end.point_number])
        self.assertEqual(len(offset_segments[0].survey_points), 3)
        self.assertAlmostEqual(offset_segments[0].survey_points[1][0], 17.5)
        self.assertAlmostEqual(offset_segments[0].survey_points[1][1], 12.5)

        line_point_numbers = [segment.point_numbers for segment in project.figure_segments if segment.segment_kind == "Line"]
        self.assertNotIn([offset_start.point_number, offset_end.point_number], line_point_numbers)
        self.assertNotIn([offset_end.point_number, offset_bend_end.point_number], line_point_numbers)
        self.assertIn([normal_start.point_number, normal_end.point_number], line_point_numbers)

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            project_path = root / "persistent-offset.cstopo"
            csv_path = root / "persistent-offset.csv"
            project.save(project_path)
            loaded = CSTopoProject.load(project_path)
            export_csv(loaded, csv_path)

            self.assertEqual(loaded.schema_version, "1.5")
            self.assertEqual(loaded.shots[2].code, "BLDPAD ST OH 2.5")
            self.assertEqual(loaded.shots[3].code, "BLDPAD OH 2.5")
            self.assertEqual(loaded.shots[4].code, "BLDPAD OH 2.5")
            self.assertTrue(loaded.shots[2].starts_new_figure)
            with csv_path.open(newline="", encoding="utf-8") as handle:
                rows = list(csv.DictReader(handle))
            self.assertEqual(rows[2]["Code"], "BLDPAD ST OH 2.5")
            self.assertEqual(rows[3]["Code"], "BLDPAD OH 2.5")
            self.assertEqual(rows[4]["Code"], "BLDPAD OH 2.5")

    def test_npc_npt_requires_intermediate_curve_point(self):
        project = CSTopoProject.default("Non Tangent Arc")
        project.add_shot(0.0, 0.0, 100.0, code="BLDPAD ST")
        npc = project.add_shot(10.0, 0.0, 100.0, code="BLDPAD NPC")
        project.add_shot(20.0, 0.0, 100.0, code="BLDPAD NPT")
        self.assertNotIn("Arc", {segment.segment_kind for segment in project.figure_segments})

        project = CSTopoProject.default("Non Tangent Arc With Mid")
        project.add_shot(0.0, 0.0, 100.0, code="BLDPAD ST")
        npc = project.add_shot(10.0, 0.0, 100.0, code="BLDPAD NPC")
        mid = project.add_shot(15.0, 5.0, 100.0, code="BLDPAD")
        npt = project.add_shot(20.0, 0.0, 100.0, code="BLDPAD NPT")
        arcs = [segment for segment in project.figure_segments if segment.segment_kind == "Arc"]
        self.assertEqual(len(arcs), 1)
        self.assertEqual(arcs[0].point_numbers, [npc.point_number, mid.point_number, npt.point_number])
        self.assertAlmostEqual(arcs[0].survey_points[0][0], npc.northing)
        self.assertAlmostEqual(arcs[0].survey_points[-1][0], npt.northing)

    def test_ssc_creates_live_smooth_curve_until_esc(self):
        project = CSTopoProject.default("Smooth Curve")
        project.add_shot(0.0, 0.0, 100.0, code="BLDPAD ST")
        ssc = project.add_shot(10.0, 0.0, 100.0, code="BLDPAD SSC")
        middle = project.add_shot(15.0, 4.0, 100.0, code="BLDPAD")

        curves = [segment for segment in project.figure_segments if segment.segment_kind == "SmoothCurve"]
        self.assertEqual(len(curves), 1)
        self.assertEqual(curves[0].point_numbers, [ssc.point_number, middle.point_number])

        esc = project.add_shot(20.0, 0.0, 100.0, code="BLDPAD ESC")
        curves = [segment for segment in project.figure_segments if segment.segment_kind == "SmoothCurve"]
        self.assertEqual(len(curves), 1)
        self.assertEqual(curves[0].point_numbers, [ssc.point_number, middle.point_number, esc.point_number])
        self.assertEqual(curves[0].control_code, "ESC")
        self.assertGreater(len(curves[0].survey_points), 3)

    def test_rect_uses_next_two_shots_and_then_starts_new_line(self):
        project = CSTopoProject.default("Rectangle")
        project.add_shot(-10.0, 0.0, 100.0, code="BLDPAD ST")
        rect = project.add_shot(0.0, 0.0, 100.0, code="BLDPAD RECT")
        length = project.add_shot(10.0, 0.0, 100.0, code="BLDPAD")
        width = project.add_shot(10.0, 5.0, 100.0, code="BLDPAD")
        next_line_start = project.add_shot(20.0, 5.0, 100.0, code="BLDPAD")
        next_line_end = project.add_shot(30.0, 5.0, 100.0, code="BLDPAD")

        rectangles = [segment for segment in project.figure_segments if segment.segment_kind == "Rectangle"]
        self.assertEqual(len(rectangles), 1)
        self.assertEqual(rectangles[0].point_numbers, [rect.point_number, length.point_number, width.point_number])
        self.assertEqual(rectangles[0].survey_points[0], (0.0, 0.0, 100.0))
        self.assertEqual(rectangles[0].survey_points[1], (10.0, 0.0, 100.0))
        self.assertEqual(rectangles[0].survey_points[2], (10.0, 5.0, 100.0))
        self.assertEqual(rectangles[0].survey_points[3], (0.0, 5.0, 100.0))

        line_point_numbers = [segment.point_numbers for segment in project.figure_segments if segment.segment_kind == "Line"]
        self.assertNotIn([width.point_number, next_line_start.point_number], line_point_numbers)
        self.assertIn([next_line_start.point_number, next_line_end.point_number], line_point_numbers)

    def test_control_code_csv_dxf_export_layers(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            project = CSTopoProject.default("Control Exports")
            project.add_shot(0.0, 0.0, 100.0, code="BLDPAD ST")
            project.add_shot(10.0, 0.0, 100.0, code="BLDPAD")
            project.add_shot(10.0, 10.0, 100.0, code="BLDPAD JPT 1")

            csv_path = root / "controls.csv"
            dxf_path = root / "controls.dxf"
            export_csv(project, csv_path)
            export_dxf(project, dxf_path)

            with csv_path.open(newline="", encoding="utf-8") as handle:
                rows = list(csv.DictReader(handle))
            self.assertEqual(rows[0]["Code"], "BLDPAD ST")
            self.assertEqual(rows[2]["Code"], "BLDPAD JPT")

            dxf = dxf_path.read_text(encoding="utf-8")
            self.assertEqual(dxf.count("\nPOINT\n"), 3)
            self.assertGreaterEqual(dxf.count("\nPOLYLINE\n"), 2)
            self.assertIn("\n8\nBLDPAD\n", dxf)
            self.assertNotIn("\n8\nBLDPAD ST\n", dxf)

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
            self.assertGreaterEqual(len(loaded.code_palette), 323)
            axle = next(style for style in loaded.code_palette if style.code == "AXLE")
            self.assertEqual(axle.category, "MONUMENT")
            self.assertEqual(axle.point_type, "Point (no triangulation)")
            with csv_path.open(newline="", encoding="utf-8") as handle:
                rows = list(csv.DictReader(handle))
            self.assertEqual(rows[0]["Code"], "BLDPAD")
            self.assertEqual(rows[-1]["Code"], "DECK")

            dxf = dxf_path.read_text(encoding="utf-8")
            self.assertIn("POLYLINE", dxf)
            self.assertIn("POINT", dxf)
            self.assertIn("BLDPAD", dxf)
            self.assertIn("DECK", dxf)

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
            self.assertIn("build_timings_seconds", progress)
            self.assertIn("las_read", progress["build_timings_seconds"])
            self.assertIn("tile_writing", progress["build_timings_seconds"])
            self.assertIn("build_memory_megabytes", progress)
            self.assertIn("peak_memory_megabytes", progress)
            self.assertIn("memory_estimate_megabytes", progress)
            self.assertIn("audit_memory_mode", progress)
            self.assertIn("audit_estimate_megabytes", progress)
            self.assertIn("audit_memory_budget_megabytes", progress)
            surface_manifest = json.loads(Path(project.derived_surfaces[0].manifest_path).read_text(encoding="utf-8"))
            self.assertIn("build_timings_seconds", surface_manifest)
            self.assertIn("build_memory_megabytes", surface_manifest)
            self.assertIn("peak_memory_megabytes", surface_manifest)
            self.assertIn("memory_estimate_megabytes", surface_manifest)
            self.assertIn("audit_memory_mode", surface_manifest)
            self.assertIn("audit_estimate_megabytes", surface_manifest)
            self.assertIn("audit_memory_budget_megabytes", surface_manifest)
            self.assertEqual(surface_manifest["schema_version"], SURFACE_MANIFEST_SCHEMA_VERSION)
            self.assertTrue(all(tile["mesh_format"] == "CSTopoBinaryTIN1" for tile in surface_manifest["tiles"]))
            self.assertTrue(all(tile["mesh_path"].endswith(".cstin") for tile in surface_manifest["tiles"]))

    def test_surface_progress_write_is_best_effort_when_replace_is_locked(self):
        with tempfile.TemporaryDirectory() as temp:
            progress_path = Path(temp) / "surface_progress.json"
            original_replace = Path.replace

            def flaky_replace(self, target):
                if self.name.startswith("surface_progress.json."):
                    raise PermissionError(5, "Access is denied")
                return original_replace(self, target)

            with mock.patch.object(Path, "replace", flaky_replace):
                write_surface_build_progress(progress_path, 0.5, "Testing", "Progress writes should not fail.", 1, 2)

            progress = json.loads(progress_path.read_text(encoding="utf-8"))
            self.assertEqual(progress["stage"], "Testing")
            self.assertEqual(progress["current"], 1)
            self.assertFalse(any(path.name.endswith(".tmp") for path in progress_path.parent.iterdir()))

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

    def test_array_global_tin_audit_matches_tuple_partition(self):
        points = [
            (float(x), float(y), 20.0 + x * 0.03 + y * 0.02)
            for x in range(0, 16)
            for y in range(0, 8)
        ]

        tuple_triangles_by_owner, tuple_boundary_edges_by_owner, tuple_diagnostics, tuple_status, tuple_message = _build_global_tin_tile_data(
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
        vertices_xyz, array_triangles_by_owner, array_boundary_counts, array_diagnostics, array_status, array_message = _build_global_tin_array_tile_data(
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

        self.assertEqual(array_status, tuple_status, array_message or tuple_message)
        self.assertEqual(array_diagnostics.retained_triangle_count, tuple_diagnostics.retained_triangle_count)
        self.assertEqual(array_diagnostics.rejected_large_triangle_count, tuple_diagnostics.rejected_large_triangle_count)
        self.assertEqual(array_diagnostics.rejected_bounds_count, tuple_diagnostics.rejected_bounds_count)
        self.assertEqual(array_diagnostics.rejected_z_delta_count, tuple_diagnostics.rejected_z_delta_count)
        self.assertEqual(set(array_triangles_by_owner), set(tuple_triangles_by_owner))
        for owner, tuple_triangles in tuple_triangles_by_owner.items():
            array_triangles = array_triangles_by_owner[owner]
            self.assertEqual(len(array_triangles), len(tuple_triangles))
            array_keys = {
                tuple(sorted(tuple(round(float(value), 6) for value in vertices_xyz[index]) for index in triangle))
                for triangle in array_triangles
            }
            tuple_keys = {
                tuple(sorted(tuple(round(float(value), 6) for value in point) for point in triangle))
                for triangle in tuple_triangles
            }
            self.assertEqual(array_keys, tuple_keys)
            owner_id = (owner[0] * (1 + 1)) + owner[1]
            self.assertEqual(array_boundary_counts.get(owner_id, 0), len(tuple_boundary_edges_by_owner.get(owner, [])))

    def test_packed_array_audit_detects_duplicate_and_nonmanifold_edges(self):
        import numpy as np

        triangle_indices = np.array(
            [
                [0, 1, 2],
                [2, 1, 0],
                [0, 1, 3],
                [1, 0, 4],
            ],
            dtype=np.uint32,
        )

        _unique_triangle_keys, triangle_key_counts = np.unique(
            _structured_triangle_keys(triangle_indices),
            return_counts=True,
        )
        self.assertEqual(int(np.sum(triangle_key_counts - 1)), 1)
        first_indices, duplicate_count = _unique_triangle_first_indices_and_duplicate_count(triangle_indices)
        self.assertEqual(duplicate_count, 1)
        self.assertEqual(sorted(int(index) for index in first_indices), [0, 2, 3])
        chunked_first_indices, chunked_duplicate_count = _unique_triangle_first_indices_and_duplicate_count(
            triangle_indices,
            chunk_triangle_count=2,
        )
        self.assertEqual(chunked_duplicate_count, duplicate_count)
        self.assertEqual(sorted(int(index) for index in chunked_first_indices), sorted(int(index) for index in first_indices))

        edge_a = triangle_indices[:, (0, 1, 2)].reshape(-1)
        edge_b = triangle_indices[:, (1, 2, 0)].reshape(-1)
        _unique_edges, edge_counts = np.unique(_packed_undirected_edge_keys(edge_a, edge_b), return_counts=True)
        self.assertEqual(int(np.count_nonzero(edge_counts > 2)), 1)
        self.assertEqual(_nonmanifold_edge_count_for_triangle_indices(triangle_indices, chunk_triangle_count=2), 1)
        memory_first_indices, memory_duplicate_count = _unique_triangle_first_indices_and_duplicate_count(
            triangle_indices,
            chunk_triangle_count=2,
            chunk_storage="memory",
        )
        self.assertEqual(memory_duplicate_count, duplicate_count)
        self.assertEqual(sorted(int(index) for index in memory_first_indices), sorted(int(index) for index in first_indices))
        self.assertEqual(
            _nonmanifold_edge_count_for_triangle_indices(triangle_indices, chunk_triangle_count=2, chunk_storage="memory"),
            1,
        )

    def test_tin_audit_memory_planner_adapts_to_ram_and_overrides(self):
        silicon_ranch_triangle_count = 9_191_625

        self.assertEqual(
            _plan_tin_audit_memory(
                silicon_ranch_triangle_count,
                physical_memory_megabytes=16_384.0,
                current_memory_megabytes=1_500.0,
            ).mode,
            "inMemory",
        )
        self.assertEqual(
            _plan_tin_audit_memory(
                25_000_000,
                physical_memory_megabytes=16_384.0,
                current_memory_megabytes=1_500.0,
            ).mode,
            "chunkedMemory",
        )
        self.assertEqual(
            _plan_tin_audit_memory(
                60_000_000,
                physical_memory_megabytes=16_384.0,
                current_memory_megabytes=1_500.0,
            ).mode,
            "chunkedDisk",
        )
        self.assertEqual(
            _plan_tin_audit_memory(
                60_000_000,
                requested_mode="in-memory",
                physical_memory_megabytes=16_384.0,
                current_memory_megabytes=1_500.0,
            ).mode,
            "inMemory",
        )
        self.assertEqual(
            _plan_tin_audit_memory(
                silicon_ranch_triangle_count,
                requested_mode="chunked-disk",
                physical_memory_megabytes=131_072.0,
                current_memory_megabytes=1_500.0,
            ).mode,
            "chunkedDisk",
        )

    def test_packed_xy_keys_are_used_when_coordinate_range_fits(self):
        import numpy as np

        points = np.asarray(
            [
                [108743.24, 69225.01, 1.0],
                [108743.24, 69225.01, 2.0],
                [111196.19, 70813.63, 3.0],
            ],
            dtype=np.float64,
        )

        keys = _packed_xy_keys_from_array(points)
        self.assertIsNotNone(keys)
        self.assertEqual(len(set(int(item) for item in keys)), 2)

    def test_owner_boundary_count_ignores_internal_edges(self):
        import numpy as np

        owner_triangles = np.array(
            [
                [0, 1, 2],
                [1, 3, 2],
            ],
            dtype=np.uint32,
        )

        self.assertEqual(_boundary_edge_count_for_triangle_indices(owner_triangles), 4)

    def test_numpy_tin_index_filter_rejects_same_triangle_classes(self):
        import numpy as np

        vertices = np.asarray(
            [
                (0.0, 0.0, 10.0),
                (1.0, 0.0, 10.1),
                (0.0, 1.0, 10.2),
                (20.0, 20.0, 10.0),
                (21.0, 20.0, 10.0),
                (20.0, 21.0, 10.0),
                (10.0, 0.0, 10.0),
                (0.0, 10.0, 10.0),
                (2.0, 2.0, 10.0),
                (3.0, 2.0, 20.0),
                (2.0, 3.0, 10.0),
            ],
            dtype=np.float64,
        )
        triangles = np.asarray(
            [
                (0, 1, 2),
                (3, 4, 5),
                (0, 6, 7),
                (8, 9, 10),
            ],
            dtype=np.uint32,
        )

        retained, diagnostics = _filter_tin_triangle_indices_numpy(
            vertices,
            triangles,
            0.0,
            0.0,
            10.0,
            10.0,
            5.0,
            1,
            1,
            5.0,
            0.75,
            5.0,
        )

        self.assertEqual(retained.tolist(), [[0, 1, 2]])
        self.assertEqual(diagnostics.retained_triangle_count, 1)
        self.assertEqual(diagnostics.rejected_bounds_count, 1)
        self.assertEqual(diagnostics.rejected_large_triangle_count, 1)
        self.assertEqual(diagnostics.rejected_z_delta_count, 1)

    def test_memory_estimate_and_auto_workers_are_conservative_for_large_builds(self):
        small_estimate = _estimate_global_tin_memory_megabytes(1_000)
        large_estimate = _estimate_global_tin_memory_megabytes(8_000_000)

        self.assertGreater(small_estimate, 0.0)
        self.assertGreater(large_estimate, small_estimate)
        self.assertEqual(_resolve_surface_build_worker_count(0, 12, 0.0, 600_000), 2)
        self.assertEqual(_resolve_surface_build_worker_count(1, 12, large_estimate, 600_000), 1)

    def test_numpy_tin_filter_matches_python_filter(self):
        raw_triangles = [
            ((0.0, 0.0, 10.0), (1.0, 0.0, 10.1), (0.0, 1.0, 10.2)),
            ((20.0, 20.0, 10.0), (21.0, 20.0, 10.0), (20.0, 21.0, 10.0)),
            ((0.0, 0.0, 10.0), (10.0, 0.0, 10.0), (0.0, 10.0, 10.0)),
            ((2.0, 2.0, 10.0), (3.0, 2.0, 20.0), (2.0, 3.0, 10.0)),
        ]

        try:
            numpy_retained, numpy_diagnostics = _filter_tin_triangles_numpy(
                raw_triangles,
                0.0,
                0.0,
                10.0,
                10.0,
                5.0,
                1,
                1,
                5.0,
                0.75,
                5.0,
            )
        except RuntimeError as exc:
            self.skipTest(str(exc))
        python_retained, python_diagnostics = _filter_tin_triangles_python(
            raw_triangles,
            0.0,
            0.0,
            10.0,
            10.0,
            5.0,
            1,
            1,
            5.0,
            0.75,
            5.0,
        )

        self.assertEqual(numpy_retained, python_retained)
        self.assertEqual(numpy_diagnostics.retained_triangle_count, python_diagnostics.retained_triangle_count)
        self.assertEqual(numpy_diagnostics.rejected_bounds_count, python_diagnostics.rejected_bounds_count)
        self.assertEqual(numpy_diagnostics.rejected_large_triangle_count, python_diagnostics.rejected_large_triangle_count)
        self.assertEqual(numpy_diagnostics.rejected_z_delta_count, python_diagnostics.rejected_z_delta_count)

    def test_numpy_las_reader_matches_python_reader(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "surface_source.las"
            write_surface_test_las(source)
            header = read_las_header(source)
            try:
                numpy_points = _read_las_xyz_points_numpy(source, header)
                numpy_array = _read_las_xyz_array_numpy(source, header)
            except RuntimeError as exc:
                self.skipTest(str(exc))

            self.assertEqual(numpy_points, _read_las_xyz_points_python(source, header))
            self.assertEqual(numpy_points, [tuple(row) for row in numpy_array.tolist()])

    def test_numpy_xy_dedupe_matches_legacy_order_and_average_z(self):
        import numpy as np

        points = [
            (100.004, 200.004, 10.0),
            (100.004, 200.004, 12.0),
            (101.0, 200.0, 20.0),
            (100.006, 200.006, 30.0),
            (101.0, 200.0, 22.0),
        ]

        numpy_deduped = _dedupe_xy_points_numpy(np.asarray(points, dtype=np.float64))
        legacy_deduped = _dedupe_xy_points(points)
        self.assertEqual([tuple(row) for row in numpy_deduped.tolist()], legacy_deduped)

    def test_parallel_global_tin_tile_writes_are_deterministic(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            points = [
                (float(x), float(y), 10.0 + x * 0.05 + y * 0.02)
                for x in range(0, 21, 2)
                for y in range(0, 11, 2)
            ]
            triangles_by_owner, boundary_edges_by_owner, _diagnostics, status, message = _build_global_tin_tile_data(
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
            sorted_owner_tiles = sorted(triangles_by_owner.items())

            summaries = []
            for workers in (0, 1, 2):
                timings = {}
                tiles = _write_global_tin_surface_tiles(
                    "SOURCEID",
                    root / f"workers_{workers}",
                    sorted_owner_tiles,
                    boundary_edges_by_owner,
                    workers,
                    None,
                    timings,
                )
                summaries.append([(tile.tile_id, tile.vertex_count, tile.triangle_count, tile.bounds_min, tile.bounds_max) for tile in tiles])
                self.assertGreater(len(tiles), 1)

            self.assertEqual(summaries[0], summaries[1])
            self.assertEqual(summaries[1], summaries[2])

    def test_binary_tin_tile_round_trips_arrays(self):
        try:
            import numpy as np
        except ImportError as exc:
            self.skipTest(str(exc))

        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "tile.cstin"
            vertices = np.asarray(
                [
                    (0.0, 0.0, 10.0),
                    (1.0, 0.0, 11.0),
                    (0.0, 1.0, 12.0),
                    (1.0, 1.0, 13.0),
                ],
                dtype=np.float64,
            )
            triangles = np.asarray([(0, 1, 2), (1, 3, 2)], dtype=np.uint32)

            _write_cstin_binary_tile(path, vertices, triangles)
            read_vertices, read_triangles, bounds_min, bounds_max = _read_cstin_binary_tile(path)

            self.assertTrue(np.array_equal(read_vertices, vertices))
            self.assertTrue(np.array_equal(read_triangles, triangles))
            self.assertEqual(bounds_min, (0.0, 0.0, 10.0))
            self.assertEqual(bounds_max, (1.0, 1.0, 13.0))

    def test_parallel_binary_global_tin_tile_writes_are_deterministic(self):
        try:
            import numpy as np
        except ImportError as exc:
            self.skipTest(str(exc))

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            global_vertices = np.asarray(
                [
                    (0.0, 0.0, 10.0),
                    (1.0, 0.0, 10.1),
                    (0.0, 1.0, 10.2),
                    (1.0, 1.0, 10.3),
                    (2.0, 0.0, 10.4),
                    (2.0, 1.0, 10.5),
                ],
                dtype=np.float64,
            )
            sorted_owner_tiles = [
                ((0, 0), np.asarray([(0, 1, 2), (1, 3, 2)], dtype=np.uint32)),
                ((1, 0), np.asarray([(1, 4, 3), (4, 5, 3)], dtype=np.uint32)),
            ]
            summaries = []
            for workers in (0, 1, 2):
                tiles = _write_global_tin_binary_surface_tiles(
                    "SOURCEID",
                    root / f"workers_{workers}",
                    global_vertices,
                    sorted_owner_tiles,
                    {0: 4, 1: 4},
                    0,
                    workers,
                    None,
                    {},
                )
                summaries.append([(tile.tile_id, tile.mesh_format, tile.vertex_count, tile.triangle_count, tile.bounds_min, tile.bounds_max) for tile in tiles])
                for tile in tiles:
                    self.assertTrue(Path(tile.mesh_path).exists())
                    self.assertTrue(tile.mesh_path.endswith(".cstin"))
                    self.assertEqual(tile.mesh_format, "CSTopoBinaryTIN1")

            self.assertEqual(summaries[0], summaries[1])
            self.assertEqual(summaries[1], summaries[2])

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
