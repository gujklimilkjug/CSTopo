from __future__ import annotations

import argparse
from pathlib import Path

from cstopo_core import (
    CSTopoProject,
    build_cache_manifest,
    build_surface_cache,
    extract_runtime_window,
    create_sample_project,
    compute_runtime_window_sample_spacing,
    create_source_record,
    export_csv,
    export_dxf,
    find_pdal_executable,
    read_las_header,
    read_pdal_info,
    translate_to_copc,
    write_surface_build_progress,
)


def cmd_sample(args: argparse.Namespace) -> None:
    out_dir = Path(args.out)
    project = create_sample_project()
    project.save(out_dir / "road_corridor_demo.cstopo")
    export_csv(project, out_dir / "road_corridor_demo.csv")
    export_dxf(project, out_dir / "road_corridor_demo.dxf")
    print(f"Wrote sample CSTopo project and exports to {out_dir}")


def cmd_export(args: argparse.Namespace) -> None:
    project = CSTopoProject.load(Path(args.project))
    out_dir = Path(args.out)
    export_csv(project, out_dir / f"{Path(args.project).stem}.csv")
    export_dxf(project, out_dir / f"{Path(args.project).stem}.dxf")
    print(f"Exported CSV and DXF to {out_dir}")


def cmd_las_info(args: argparse.Namespace) -> None:
    header = read_las_header(Path(args.source))
    print(f"LAS version: {header.version}")
    print(f"Point count: {header.point_count}")
    print(f"Point format: {header.point_format}")
    print(f"Point record length: {header.point_record_length}")
    print(f"Point data offset: {header.point_data_offset}")
    print(f"VLR count: {header.vlr_count}")
    print(f"Scale: {header.scale}")
    print(f"Offset: {header.offset}")
    print(f"Bounds min: {header.bounds_min}")
    print(f"Bounds max: {header.bounds_max}")
    print(f"Linear unit: {header.linear_unit_name} ({header.linear_unit_to_meters} meters)")


def cmd_import_las(args: argparse.Namespace) -> None:
    source = Path(args.source)
    out_path = Path(args.out)
    cache_dir = Path(args.cache_dir)
    project = CSTopoProject.default(source.stem)
    source_record = create_source_record(source, cache_dir, target_cache_format=args.cache_format)
    source_record.is_active = True
    project.active_point_cloud_id = source_record.source_id
    project.point_clouds.append(source_record)
    project.save(out_path)
    print(f"Wrote CSTopo project with cache-backed point cloud metadata to {out_path}")


def cmd_build_cache_manifest(args: argparse.Namespace) -> None:
    project_path = Path(args.project)
    project = CSTopoProject.load(project_path)
    manifest = build_cache_manifest(project, project_path)
    manifest_path = Path(project.cache_manifest_path)
    manifest.save(manifest_path)
    print(f"Wrote cache manifest to {manifest_path}")


def cmd_extract_runtime_window(args: argparse.Namespace) -> None:
    project = CSTopoProject.load(Path(args.project))
    source = next((item for item in project.point_clouds if item.source_id == project.active_point_cloud_id), None)
    if source is None:
        raise SystemExit("Project has no active point cloud.")
    output_path = extract_runtime_window(
        source,
        Path(args.cache_dir),
        args.center_x,
        args.center_y,
        project.runtime_streaming,
    )
    spacing = compute_runtime_window_sample_spacing(
        project.runtime_streaming.window_radius_source_units,
        project.runtime_streaming.target_point_budget,
    )
    print(f"Wrote runtime window to {output_path} (sample spacing {spacing:.3f})")


def cmd_build_surface(args: argparse.Namespace) -> None:
    project_path = Path(args.project) if args.project else None
    if project_path is not None:
        project = CSTopoProject.load(project_path)
    else:
        source = Path(args.source)
        cache_dir = Path(args.cache_dir)
        project = CSTopoProject.default(source.stem)
        record = create_source_record(source, cache_dir)
        if args.source_id:
            record.source_id = args.source_id
        record.is_active = True
        project.active_point_cloud_id = record.source_id
        project.point_clouds.append(record)

    source_id = args.source_id or project.active_point_cloud_id
    if not source_id:
        raise SystemExit("A source id is required when the project has no active point cloud.")

    if args.ground_method is not None:
        project.surface_settings.ground_classification_method = args.ground_method
    if args.surface_build_method is not None:
        project.surface_settings.surface_build_method = args.surface_build_method
    if args.tile_size_source_units is not None:
        project.surface_settings.tile_size_source_units = args.tile_size_source_units
    if args.tile_padding_source_units is not None:
        project.surface_settings.tile_padding_source_units = args.tile_padding_source_units
    if args.max_surface_fill_cells is not None:
        project.surface_settings.max_surface_fill_cells = args.max_surface_fill_cells
    if args.max_surface_triangle_z_delta_source_units is not None:
        project.surface_settings.max_surface_triangle_z_delta_source_units = args.max_surface_triangle_z_delta_source_units
    if args.tin_max_buffered_points_per_tile is not None:
        project.surface_settings.tin_max_buffered_points_per_tile = args.tin_max_buffered_points_per_tile
    if args.tin_max_edge_length_source_units is not None:
        project.surface_settings.tin_max_edge_length_source_units = args.tin_max_edge_length_source_units
    if args.tin_boundary_mode is not None:
        project.surface_settings.tin_boundary_mode = args.tin_boundary_mode
    if args.tin_boundary_edge_multiplier is not None:
        project.surface_settings.tin_boundary_edge_multiplier = args.tin_boundary_edge_multiplier
    if args.tin_boundary_area_multiplier is not None:
        project.surface_settings.tin_boundary_area_multiplier = args.tin_boundary_area_multiplier
    if args.tin_constrain_to_boundary is not None:
        project.surface_settings.tin_constrain_to_boundary = args.tin_constrain_to_boundary
    if args.tin_decimation_mode is not None:
        project.surface_settings.tin_decimation_mode = args.tin_decimation_mode
    if args.global_tin_max_points is not None:
        project.surface_settings.global_tin_max_points = args.global_tin_max_points
    if args.surface_collision_mode is not None:
        project.surface_settings.surface_collision_mode = args.surface_collision_mode

    cache_dir = Path(args.cache_dir) if args.cache_dir else Path(project.cache_manifest_path).parent / "cache"
    progress_path = Path(args.progress_path) if args.progress_path else None
    try:
        manifest = build_surface_cache(
            project,
            source_id,
            cache_dir,
            force_rebuild=args.force,
            progress_path=progress_path,
            surface_build_workers=args.surface_build_workers,
            surface_audit_mode=args.surface_audit_mode,
            surface_audit_memory_fraction=args.surface_audit_memory_fraction,
        )
    except Exception as exc:
        write_surface_build_progress(progress_path, 1.0, "Failed", f"Surface build failed: {exc}", 1, 1)
        raise
    if project_path is not None:
        project.save(project_path)
    print(f"Wrote surface manifest to {manifest.manifest_path}")


def cmd_pdal_info(args: argparse.Namespace) -> None:
    print(find_pdal_executable())
    if args.source:
        info = read_pdal_info(Path(args.source))
        summary = info.get("summary", {})
        metadata = summary.get("metadata", {})
        print(f"Reader: {info.get('reader', '')}")
        print(f"Point count: {summary.get('num_points', metadata.get('count', ''))}")
        print(f"Bounds: {summary.get('bounds', {})}")


def cmd_translate_copc(args: argparse.Namespace) -> None:
    output_path = translate_to_copc(Path(args.source), Path(args.cache_dir))
    print(f"Wrote COPC cache to {output_path}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="CSTopo survey project utility")
    subcommands = parser.add_subparsers(required=True)

    sample = subcommands.add_parser("sample", help="create a sample project with CSV and DXF exports")
    sample.add_argument("--out", default="demo", help="output directory")
    sample.set_defaults(func=cmd_sample)

    export = subcommands.add_parser("export", help="export an existing .cstopo project")
    export.add_argument("project", help="path to .cstopo project")
    export.add_argument("--out", default="exports", help="output directory")
    export.set_defaults(func=cmd_export)

    las_info = subcommands.add_parser("las-info", help="print LAS header metadata")
    las_info.add_argument("source", help="path to .las, .laz, or .copc.laz file")
    las_info.set_defaults(func=cmd_las_info)

    import_las = subcommands.add_parser("import-las", help="create a .cstopo project containing cache-backed LAS source metadata")
    import_las.add_argument("source", help="path to .las, .laz, or .copc.laz file")
    import_las.add_argument("--out", required=True, help="output .cstopo project path")
    import_las.add_argument("--cache-dir", default="cache", help="directory for future COPC cache output")
    import_las.add_argument("--cache-format", default="COPC", choices=["COPC", "DirectLasLaz"], help="target cache format")
    import_las.set_defaults(func=cmd_import_las)

    cache_manifest = subcommands.add_parser("build-cache-manifest", help="rebuild the point-cloud cache manifest for a .cstopo project")
    cache_manifest.add_argument("project", help="path to .cstopo project")
    cache_manifest.set_defaults(func=cmd_build_cache_manifest)

    extract_window = subcommands.add_parser("extract-runtime-window", help="extract a camera-local runtime LAS window from the active COPC cache")
    extract_window.add_argument("project", help="path to .cstopo project")
    extract_window.add_argument("center_x", type=float, help="runtime window center X in source coordinates")
    extract_window.add_argument("center_y", type=float, help="runtime window center Y in source coordinates")
    extract_window.add_argument("--cache-dir", default="cache", help="directory for generated runtime window files")
    extract_window.set_defaults(func=cmd_extract_runtime_window)

    build_surface = subcommands.add_parser("build-surface", help="build a tiled derived surface cache")
    build_surface.add_argument("project", nargs="?", help="optional .cstopo project path")
    build_surface.add_argument("--source", help="direct source LAS/LAZ/COPC path when not using a project")
    build_surface.add_argument("--source-id", help="point cloud source id in the project")
    build_surface.add_argument("--cache-dir", default="cache", help="directory for generated surface files")
    build_surface.add_argument("--surface-build-method", choices=["GlobalDelaunayTIN", "BufferedDelaunayTIN"], help="surface TIN build method")
    build_surface.add_argument("--ground-method", choices=["SMRF", "PMF", "existingClassification"], help="ground classification method")
    build_surface.add_argument("--tile-size-source-units", type=float, help="non-overlapping tile width/height in source units")
    build_surface.add_argument("--tile-padding-source-units", type=float, help="sample-only tile padding in source units")
    build_surface.add_argument("--max-surface-fill-cells", type=int, help="maximum grid-cell distance used to fill unsupported surface cells")
    build_surface.add_argument("--max-surface-triangle-z-delta-source-units", type=float, help="maximum triangle corner elevation delta in source units")
    build_surface.add_argument("--tin-max-buffered-points-per-tile", type=int, help="maximum buffered TIN points before adaptive decimation")
    build_surface.add_argument("--tin-max-edge-length-source-units", type=float, help="maximum accepted TIN edge length in source units")
    build_surface.add_argument("--tin-boundary-mode", choices=["SupportBoundary"], help="TIN trim-boundary derivation mode")
    build_surface.add_argument("--tin-boundary-edge-multiplier", type=float, help="auto edge threshold multiplier for support-boundary trimming")
    build_surface.add_argument("--tin-boundary-area-multiplier", type=float, help="maximum triangle area as a multiplier of the squared resolved edge threshold")
    build_surface.add_argument("--tin-constrain-to-boundary", action=argparse.BooleanOptionalAction, help="constrain TIN output to extracted support-boundary segments")
    build_surface.add_argument("--tin-decimation-mode", choices=["PreserveExtremesAdaptive"], help="TIN decimation mode used only above the point cap")
    build_surface.add_argument("--global-tin-max-points", type=int, help="maximum unique ground points accepted by GlobalDelaunayTIN")
    build_surface.add_argument("--surface-collision-mode", choices=["RuntimeVisibleStitchedTIN"], help="runtime collision strategy for derived surface tiles")
    build_surface.add_argument("--surface-build-workers", type=int, default=0, help="tile-writing worker count; 0 chooses a conservative automatic count")
    build_surface.add_argument("--surface-audit-mode", choices=["auto", "in-memory", "chunked-disk"], default="auto", help="debug override for full topology audit memory strategy")
    build_surface.add_argument("--surface-audit-memory-fraction", type=float, default=0.45, help="fraction of physical RAM the automatic audit planner may budget")
    build_surface.add_argument("--progress-path", help="optional JSON file updated with surface build progress")
    build_surface.add_argument("--force", action="store_true", help="rebuild surface even if a manifest already exists")
    build_surface.set_defaults(func=cmd_build_surface)

    pdal_info = subcommands.add_parser("pdal-info", help="print the PDAL executable and optional source summary")
    pdal_info.add_argument("source", nargs="?", help="optional LAS/LAZ/COPC file to inspect with PDAL")
    pdal_info.set_defaults(func=cmd_pdal_info)

    translate_copc = subcommands.add_parser("translate-copc", help="convert LAS/LAZ to a COPC cache using PDAL")
    translate_copc.add_argument("source", help="path to source .las or .laz file")
    translate_copc.add_argument("--cache-dir", default="cache", help="directory for the generated .copc.laz file")
    translate_copc.set_defaults(func=cmd_translate_copc)

    return parser


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
