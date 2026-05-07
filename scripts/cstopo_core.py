from __future__ import annotations

import csv
import json
import math
import os
import shutil
import struct
import subprocess
import uuid
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Set, Tuple


Point3 = Tuple[float, float, float]
DEFAULT_QGIS_PDAL = Path(r"C:\Program Files\QGIS 3.40.12\bin\pdal.exe")
SURFACE_MANIFEST_SCHEMA_VERSION = "5.0"
SURFACE_BUILD_METHOD_TIN = "BufferedDelaunayTIN"
SURFACE_BUILD_METHOD_GLOBAL_TIN = "GlobalDelaunayTIN"
SOURCE_POINT_PRECISION = 0.01
TIN_BOUNDARY_MODE_SUPPORT = "SupportBoundary"
SURFACE_COLLISION_MODE_STITCHED_TIN = "RuntimeVisibleStitchedTIN"
BUILT_IN_CODE_LIST_PATH = Path(__file__).resolve().parents[1] / "Config" / "CSTopoCodeList.json"
BUILT_IN_CONTROL_CODE_LIST_PATH = Path(__file__).resolve().parents[1] / "Config" / "CSTopoControlCodeList.json"


@dataclass
class PointCloudSource:
    source_id: str
    source_path: str
    cache_path: str
    cache_format: str = "COPC"
    point_count: int = 0
    bounds_min: Point3 = (0.0, 0.0, 0.0)
    bounds_max: Point3 = (0.0, 0.0, 0.0)
    coordinate_system_wkt: str = ""
    linear_unit_name: str = "unknown"
    linear_unit_to_meters: float = 0.0
    direct_open_point_threshold: int = 5_000_000
    direct_open_eligible: bool = True
    source_exists: bool = True
    cache_exists: bool = False
    cache_preferred_for_runtime: bool = False
    cache_out_of_date: bool = False
    runtime_display_path: str = ""
    cache_status: str = "No cache state available."
    cache_state: str = "None"
    source_modified_at: str = ""
    cache_modified_at: str = ""
    runtime_window_active: bool = False
    runtime_window_pending: bool = False
    runtime_window_path: str = ""
    runtime_window_status: str = "Runtime window inactive."
    runtime_window_center: Point3 = (0.0, 0.0, 0.0)
    runtime_window_radius: float = 0.0
    runtime_window_sample_spacing: float = 0.0
    loaded: bool = True
    visible: bool = True
    is_active: bool = False
    surface_id: str = ""
    surface_manifest_path: str = ""
    surface_status: str = "Surface not built."
    surface_build_state: str = "None"
    surface_build_progress: float = 0.0
    surface_build_progress_stage: str = ""
    surface_build_progress_message: str = ""
    surface_primary: bool = True
    surface_render_visible: bool = True
    cloud_overlay_visible: bool = True
    default_view_mode: str = "SurfaceShaded"


@dataclass
class LasHeaderInfo:
    version: str
    header_size: int
    point_data_offset: int
    vlr_count: int
    point_format: int
    point_record_length: int
    point_count: int
    scale: Point3
    offset: Point3
    bounds_min: Point3
    bounds_max: Point3
    coordinate_system_wkt: str = ""
    linear_unit_name: str = "unknown"
    linear_unit_to_meters: float = 0.0


@dataclass
class CodeStyle:
    code: str
    layer_name: str
    color: str = "white"
    visible: bool = True
    display_name: str = ""
    category: str = ""
    point_type: str = ""


@dataclass
class ControlCodeDefinition:
    name: str
    code: str
    action: str
    parameter_kind: str = "None"
    geometry_kind: str = "None"


@dataclass
class ParsedControlCommand:
    base_code: str
    control_code: str = ""
    parameter: str = ""


def load_builtin_code_palette() -> List[CodeStyle]:
    try:
        raw = json.loads(BUILT_IN_CODE_LIST_PATH.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return []

    palette: List[CodeStyle] = []
    for item in raw.get("codes", []):
        code = str(item.get("code", "")).strip().upper()
        if not code:
            continue
        palette.append(
            CodeStyle(
                code=code,
                layer_name=code,
                color=str(item.get("color", "white")),
                visible=True,
                display_name=str(item.get("name", "")),
                category=str(item.get("category", "")),
                point_type=str(item.get("point_type", "")),
            )
        )
    return palette


def load_control_code_definitions() -> List[ControlCodeDefinition]:
    try:
        raw = json.loads(BUILT_IN_CONTROL_CODE_LIST_PATH.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return []

    controls: List[ControlCodeDefinition] = []
    for item in raw.get("controls", []):
        code = str(item.get("code", "")).strip().upper()
        if not code:
            continue
        controls.append(
            ControlCodeDefinition(
                name=str(item.get("name", "")).strip(),
                code=code,
                action=str(item.get("action", "")).strip(),
                parameter_kind=str(item.get("parameter_kind", "None")).strip() or "None",
                geometry_kind=str(item.get("geometry_kind", "None")).strip() or "None",
            )
        )
    return controls


def control_definition(code: str) -> Optional[ControlCodeDefinition]:
    normalized = code.strip().upper()
    for control in load_control_code_definitions():
        if control.code == normalized:
            return control
    return None


def parse_control_command(text: str, active_code: str = "") -> ParsedControlCommand:
    tokens = [part.strip().upper() for part in text.split() if part.strip()]
    if not tokens:
        return ParsedControlCommand(active_code.strip().upper())
    if control_definition(tokens[0]) is not None:
        return ParsedControlCommand(
            base_code=active_code.strip().upper(),
            control_code=tokens[0],
            parameter=tokens[1] if len(tokens) > 1 else "",
        )
    if len(tokens) >= 2 and control_definition(tokens[1]) is not None:
        return ParsedControlCommand(
            base_code=tokens[0],
            control_code=tokens[1],
            parameter=tokens[2] if len(tokens) > 2 else "",
        )
    return ParsedControlCommand(base_code=tokens[0])


def measurement_display_code(base_code: str, control_code: str = "") -> str:
    base = base_code.strip().upper()
    control = control_code.strip().upper()
    return f"{base} {control}" if control else base


def builtin_code_style(code: str) -> Optional[CodeStyle]:
    normalized = code.strip().upper()
    for style in load_builtin_code_palette():
        if style.code.upper() == normalized:
            return style
    return None


def normalize_point_type(point_type: str) -> str:
    return point_type.strip().replace("_", " ").upper()


def point_type_creates_figure_linework(point_type: str) -> bool:
    normalized = normalize_point_type(point_type)
    return normalized in {"BREAKLINE", "LINE", "LINE FEATURE"}


def point_type_contributes_to_user_tin(point_type: str) -> bool:
    normalized = normalize_point_type(point_type)
    if "NO TRIANGULATION" in normalized:
        return False
    return normalized in {"", "POINT", "BREAKLINE"}


def point_type_creates_tin_breakline(point_type: str) -> bool:
    return normalize_point_type(point_type) == "BREAKLINE"


@dataclass
class RuntimeStreamingSettings:
    enable_copc_window_streaming: bool = True
    window_radius_source_units: float = 350.0
    window_refresh_distance_source_units: float = 175.0
    vertical_padding_source_units: float = 25.0
    target_point_budget: int = 750_000
    update_interval_seconds: float = 0.75


@dataclass
class SurfaceSettings:
    surface_build_on_import: bool = True
    surface_primary: bool = True
    cloud_overlay_visible: bool = True
    default_view_mode: str = "SurfaceShaded"
    surface_build_method: str = SURFACE_BUILD_METHOD_GLOBAL_TIN
    ground_classification_method: str = "SMRF"
    tile_size_source_units: float = 250.0
    tile_padding_source_units: float = 25.0
    max_grid_vertices_per_tile: int = 40000
    max_surface_fill_cells: int = 2
    max_surface_triangle_z_delta_source_units: float = 0.0
    tin_max_buffered_points_per_tile: int = 150_000
    tin_max_edge_length_source_units: float = 0.0
    tin_boundary_mode: str = TIN_BOUNDARY_MODE_SUPPORT
    tin_boundary_edge_multiplier: float = 8.0
    tin_boundary_area_multiplier: float = 0.75
    tin_constrain_to_boundary: bool = True
    tin_decimation_mode: str = "PreserveExtremesAdaptive"
    global_tin_max_points: int = 5_000_000
    surface_collision_mode: str = SURFACE_COLLISION_MODE_STITCHED_TIN


@dataclass
class SurfaceTileRecord:
    tile_id: str
    bounds_min: Point3
    bounds_max: Point3
    vertex_count: int = 0
    triangle_count: int = 0
    mesh_path: str = ""
    status: str = "Pending"
    input_point_count: int = 0
    used_point_count: int = 0
    decimation_method: str = "none"
    boundary_edge_count: int = 0
    boundary_loop_count: int = 0


@dataclass
class DerivedSurfaceManifest:
    surface_id: str
    source_cloud_id: str
    schema_version: str = SURFACE_MANIFEST_SCHEMA_VERSION
    build_state: str = "Pending"
    build_method: str = SURFACE_BUILD_METHOD_GLOBAL_TIN
    ground_source: str = "existingClassification"
    tile_size: float = 250.0
    tile_count: int = 0
    bounds_min: Point3 = (0.0, 0.0, 0.0)
    bounds_max: Point3 = (0.0, 0.0, 0.0)
    source_crs: str = ""
    linear_unit_name: str = "unknown"
    linear_unit_to_meters: float = 0.0
    generated_at: str = ""
    manifest_path: str = ""
    tiles: List[SurfaceTileRecord] = field(default_factory=list)
    total_vertex_count: int = 0
    total_triangle_count: int = 0
    decimation_used: bool = False
    tin_boundary_mode: str = TIN_BOUNDARY_MODE_SUPPORT
    tin_resolved_max_edge_length_source_units: float = 0.0
    tin_boundary_edge_multiplier: float = 8.0
    tin_boundary_area_multiplier: float = 0.75
    tin_rejected_large_triangle_count: int = 0
    tin_retained_candidate_triangle_count: int = 0
    tin_boundary_edge_count: int = 0
    tin_boundary_loop_count: int = 0
    tin_constrained_triangulation_status: str = "NotRun"
    global_point_count: int = 0
    global_retained_triangle_count: int = 0
    global_rejected_triangle_count: int = 0
    topology_audit_status: str = "NotRun"
    topology_audit_message: str = ""
    surface_collision_mode: str = SURFACE_COLLISION_MODE_STITCHED_TIN
    seam_audit_status: str = "NotRun"
    seam_audit_message: str = ""


@dataclass
class ShotRecord:
    point_number: int
    northing: float
    easting: float
    elevation: float
    code: str
    figure_id: str
    base_code: str = ""
    control_code: str = ""
    control_parameter: str = ""
    fit_type: str = "PlaneLeastSquares"
    fit_residual: float = 0.0
    source_cloud_id: str = ""
    created_at: str = ""
    notes: str = ""
    measurement_source: str = "DerivedSurface"
    surface_id: str = ""
    surface_tile_id: str = ""
    surface_method: str = ""


@dataclass
class FigureRecord:
    figure_id: str
    code: str
    layer_name: str
    point_numbers: List[int] = field(default_factory=list)
    closed: bool = False
    loop_closed: bool = False
    style: CodeStyle = field(default_factory=lambda: CodeStyle("", ""))


@dataclass
class FigureSegmentRecord:
    segment_id: str
    segment_kind: str
    code: str
    layer_name: str
    control_code: str = ""
    control_parameter: str = ""
    created_by_point_number: int = 0
    point_numbers: List[int] = field(default_factory=list)
    survey_points: List[Point3] = field(default_factory=list)


@dataclass
class CSTopoProject:
    schema_version: str = "1.4"
    project_name: str = "Untitled CSTopo Project"
    active_code: str = ""
    active_point_cloud_id: str = ""
    next_point_number: int = 1
    navigation_mode: str = "Walk"
    point_clouds: List[PointCloudSource] = field(default_factory=list)
    cache_manifest_path: str = ""
    runtime_streaming: RuntimeStreamingSettings = field(default_factory=RuntimeStreamingSettings)
    surface_settings: SurfaceSettings = field(default_factory=SurfaceSettings)
    derived_surfaces: List[DerivedSurfaceManifest] = field(default_factory=list)
    code_palette: List[CodeStyle] = field(default_factory=list)
    shots: List[ShotRecord] = field(default_factory=list)
    figures: List[FigureRecord] = field(default_factory=list)
    figure_segments: List[FigureSegmentRecord] = field(default_factory=list)
    pending_control_code: str = ""
    pending_control_parameter: str = ""

    @classmethod
    def default(cls, project_name: str = "Untitled CSTopo Project") -> "CSTopoProject":
        code_palette = load_builtin_code_palette()
        return cls(
            project_name=project_name,
            schema_version="1.4",
            active_code=code_palette[0].code if code_palette else "",
            code_palette=code_palette,
        )

    def style_for(self, code: str) -> CodeStyle:
        parsed = parse_control_command(code, self.active_code)
        code = parsed.base_code or code
        for style in self.code_palette:
            if style.code.upper() == code.upper():
                return style
        style = builtin_code_style(code)
        if style is not None:
            self.code_palette.append(style)
            return style
        return CodeStyle(
            code=code,
            layer_name=code,
            display_name=code,
            point_type="Point (no triangulation)",
            visible=False,
        )

    def set_pending_control_code(self, control_code: str, parameter: str = "") -> None:
        normalized = control_code.strip().upper()
        if normalized and control_definition(normalized) is None:
            raise ValueError(f"Unknown CSTopo control code: {control_code}")
        self.pending_control_code = normalized
        self.pending_control_parameter = parameter.strip().upper()

    def clear_pending_control_code(self) -> None:
        self.pending_control_code = ""
        self.pending_control_parameter = ""

    def _validate_control_parameter(self, control_code: str, parameter: str) -> None:
        definition = control_definition(control_code)
        if definition is None or definition.parameter_kind in {"None", "OptionalDistance"}:
            return
        if definition.parameter_kind == "PointNumber":
            if not parameter.upper().lstrip("P").isdigit():
                raise ValueError(f"{control_code} needs a point number parameter.")
            return
        if definition.parameter_kind == "Distance":
            try:
                float(parameter)
            except ValueError as exc:
                raise ValueError(f"{control_code} needs a numeric distance parameter.") from exc
            return
        if definition.parameter_kind == "DistanceOrPointNumber":
            if parameter.upper().startswith("P") and parameter[1:].isdigit():
                return
            try:
                float(parameter)
            except ValueError as exc:
                raise ValueError(f"{control_code} needs a distance or P<pointNumber> parameter.") from exc

    def open_figure_for(self, code: str) -> FigureRecord:
        for figure in self.figures:
            if figure.code.upper() == code.upper() and not figure.closed:
                return figure
        style = self.style_for(code)
        figure = FigureRecord(
            figure_id=str(uuid.uuid4()),
            code=code,
            layer_name=style.layer_name,
            style=style,
        )
        self.figures.append(figure)
        return figure

    def add_shot(
        self,
        northing: float,
        easting: float,
        elevation: float,
        code: Optional[str] = None,
        fit_residual: float = 0.0,
        source_cloud_id: str = "",
        notes: str = "",
    ) -> ShotRecord:
        parsed = parse_control_command(code or self.active_code, self.active_code)
        active_code = parsed.base_code or self.active_code
        control_code = parsed.control_code or self.pending_control_code
        control_parameter = parsed.parameter or self.pending_control_parameter
        self._validate_control_parameter(control_code, control_parameter)

        style = self.style_for(active_code)
        if not style.visible and builtin_code_style(active_code) is None:
            raise ValueError(f"Code is not in the CSTopo Code List: {active_code}")
        if control_code == "ST":
            self.split_figure(active_code)
        join_linework = control_code != "IG"
        figure = self.open_figure_for(active_code) if join_linework and point_type_creates_figure_linework(style.point_type) else None
        shot = ShotRecord(
            point_number=self.next_point_number,
            northing=northing,
            easting=easting,
            elevation=elevation,
            code=measurement_display_code(active_code, control_code),
            figure_id=figure.figure_id if figure is not None else "",
            base_code=active_code,
            control_code=control_code,
            control_parameter=control_parameter,
            fit_residual=fit_residual,
            source_cloud_id=source_cloud_id,
            created_at=datetime.now(timezone.utc).isoformat(),
            notes=notes,
        )
        self.next_point_number += 1
        self.active_code = active_code
        self.shots.append(shot)
        if figure is not None:
            figure.point_numbers.append(shot.point_number)
        if control_code == "END":
            self.split_figure(active_code)
        elif control_code == "CLS":
            self.close_figure(active_code)
        self.clear_pending_control_code()
        self.rebuild_figure_segments()
        return shot

    def add_fitted_shot(
        self,
        target_northing: float,
        target_easting: float,
        samples: Sequence[Point3],
        code: Optional[str] = None,
        source_cloud_id: str = "",
    ) -> ShotRecord:
        elevation, residual = fit_plane_elevation(samples, target_northing, target_easting)
        return self.add_shot(
            northing=target_northing,
            easting=target_easting,
            elevation=elevation,
            code=code,
            fit_residual=residual,
            source_cloud_id=source_cloud_id,
            notes=f"fit=PlaneLeastSquares samples={len(samples)}",
        )

    def split_figure(self, code: Optional[str] = None) -> None:
        active_code = code or self.active_code
        for figure in self.figures:
            if figure.code.upper() == active_code.upper() and not figure.closed:
                figure.closed = True
                figure.loop_closed = False

    def close_figure(self, code: Optional[str] = None) -> None:
        figure = self.open_figure_for(code or self.active_code)
        figure.closed = True
        figure.loop_closed = True

    def undo_last_measurement(self) -> bool:
        if not self.shots:
            return False
        point_number = self.shots[-1].point_number
        self.shots.pop()
        for figure in self.figures:
            figure.point_numbers = [number for number in figure.point_numbers if number != point_number]
        self.figures = [figure for figure in self.figures if figure.point_numbers]
        self.next_point_number = max([shot.point_number for shot in self.shots], default=0) + 1
        self.rebuild_figure_segments()
        return True

    def rebuild_figure_segments(self) -> None:
        self.figure_segments = build_figure_segments(self)

    def save(self, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        self.schema_version = "1.4"
        backfill_code_metadata(self)
        if not self.cache_manifest_path:
            self.cache_manifest_path = str(build_default_cache_manifest_path(path))
        manifest = build_cache_manifest(self, path)
        self.point_clouds = manifest.entries
        path.write_text(json.dumps(asdict(self), indent=2), encoding="utf-8")
        manifest.save(Path(self.cache_manifest_path))

    @classmethod
    def load(cls, path: Path) -> "CSTopoProject":
        raw = json.loads(path.read_text(encoding="utf-8"))
        project = project_from_dict(raw)
        backfill_code_metadata(project)
        if not project.cache_manifest_path:
            project.cache_manifest_path = str(build_default_cache_manifest_path(path))
        manifest_path = Path(project.cache_manifest_path)
        if manifest_path.exists():
            apply_cache_manifest(project, PointCloudCacheManifest.load(manifest_path))
        return project


@dataclass
class PointCloudCacheManifest:
    schema_version: str = "1.1"
    generated_at: str = ""
    project_path: str = ""
    entries: List[PointCloudSource] = field(default_factory=list)

    def save(self, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(asdict(self), indent=2), encoding="utf-8")

    @classmethod
    def load(cls, path: Path) -> "PointCloudCacheManifest":
        raw = json.loads(path.read_text(encoding="utf-8"))
        return cls(
            schema_version=raw.get("schema_version", "1.1"),
            generated_at=raw.get("generated_at", ""),
            project_path=raw.get("project_path", ""),
            entries=[PointCloudSource(**item) for item in raw.get("entries", [])],
        )


def derived_surface_from_dict(raw: dict) -> DerivedSurfaceManifest:
    return DerivedSurfaceManifest(
        surface_id=raw["surface_id"],
        source_cloud_id=raw["source_cloud_id"],
        schema_version=raw.get("schema_version", ""),
        build_state=raw.get("build_state", "Pending"),
        build_method=raw.get("build_method", SURFACE_BUILD_METHOD_TIN),
        ground_source=raw.get("ground_source", "existingClassification"),
        tile_size=raw.get("tile_size", 250.0),
        tile_count=raw.get("tile_count", 0),
        bounds_min=tuple(raw.get("bounds_min", (0.0, 0.0, 0.0))),
        bounds_max=tuple(raw.get("bounds_max", (0.0, 0.0, 0.0))),
        source_crs=raw.get("source_crs", ""),
        linear_unit_name=raw.get("linear_unit_name", "unknown"),
        linear_unit_to_meters=raw.get("linear_unit_to_meters", 0.0),
        generated_at=raw.get("generated_at", ""),
        manifest_path=raw.get("manifest_path", ""),
        tiles=[
            SurfaceTileRecord(
                tile_id=item["tile_id"],
                bounds_min=tuple(item.get("bounds_min", (0.0, 0.0, 0.0))),
                bounds_max=tuple(item.get("bounds_max", (0.0, 0.0, 0.0))),
                vertex_count=item.get("vertex_count", 0),
                triangle_count=item.get("triangle_count", 0),
                mesh_path=item.get("mesh_path", ""),
                status=item.get("status", "Pending"),
                input_point_count=item.get("input_point_count", 0),
                used_point_count=item.get("used_point_count", 0),
                decimation_method=item.get("decimation_method", "none"),
                boundary_edge_count=item.get("boundary_edge_count", 0),
                boundary_loop_count=item.get("boundary_loop_count", 0),
            )
            for item in raw.get("tiles", [])
        ],
        total_vertex_count=raw.get("total_vertex_count", 0),
        total_triangle_count=raw.get("total_triangle_count", 0),
        decimation_used=raw.get("decimation_used", False),
        tin_boundary_mode=raw.get("tin_boundary_mode", TIN_BOUNDARY_MODE_SUPPORT),
        tin_resolved_max_edge_length_source_units=raw.get("tin_resolved_max_edge_length_source_units", 0.0),
        tin_boundary_edge_multiplier=raw.get("tin_boundary_edge_multiplier", 8.0),
        tin_boundary_area_multiplier=raw.get("tin_boundary_area_multiplier", 0.75),
        tin_rejected_large_triangle_count=raw.get("tin_rejected_large_triangle_count", 0),
        tin_retained_candidate_triangle_count=raw.get("tin_retained_candidate_triangle_count", 0),
        tin_boundary_edge_count=raw.get("tin_boundary_edge_count", 0),
        tin_boundary_loop_count=raw.get("tin_boundary_loop_count", 0),
        tin_constrained_triangulation_status=raw.get("tin_constrained_triangulation_status", "NotRun"),
        global_point_count=raw.get("global_point_count", 0),
        global_retained_triangle_count=raw.get("global_retained_triangle_count", 0),
        global_rejected_triangle_count=raw.get("global_rejected_triangle_count", 0),
        topology_audit_status=raw.get("topology_audit_status", "NotRun"),
        topology_audit_message=raw.get("topology_audit_message", ""),
        surface_collision_mode=raw.get("surface_collision_mode", SURFACE_COLLISION_MODE_STITCHED_TIN),
        seam_audit_status=raw.get("seam_audit_status", "NotRun"),
        seam_audit_message=raw.get("seam_audit_message", ""),
    )


def code_style_from_dict(raw: dict) -> CodeStyle:
    code = raw.get("code", raw.get("Code", ""))
    layer_name = raw.get("layer_name", raw.get("layerName", raw.get("LayerName", code)))
    style = CodeStyle(
        code=code,
        layer_name=layer_name,
        color=raw.get("color", raw.get("Color", "white")),
        visible=raw.get("visible", raw.get("bVisible", raw.get("Visible", True))),
        display_name=raw.get("display_name", raw.get("displayName", raw.get("DisplayName", ""))),
        category=raw.get("category", raw.get("Category", "")),
        point_type=raw.get("point_type", raw.get("pointType", raw.get("PointType", ""))),
    )
    builtin = builtin_code_style(style.code)
    if builtin is not None:
        if not style.display_name:
            style.display_name = builtin.display_name
        if not style.category:
            style.category = builtin.category
        if not style.point_type:
            style.point_type = builtin.point_type
        if style.color in {"", "white"}:
            style.color = builtin.color
    return style


def shot_from_dict(raw: dict) -> ShotRecord:
    code = raw.get("code", raw.get("Code", ""))
    parsed = parse_control_command(code)
    base_code = raw.get("base_code", raw.get("baseCode", raw.get("BaseCode", parsed.base_code or code)))
    control_code = raw.get("control_code", raw.get("controlCode", raw.get("ControlCode", parsed.control_code)))
    control_parameter = raw.get("control_parameter", raw.get("controlParameter", raw.get("ControlParameter", parsed.parameter)))
    return ShotRecord(
        point_number=raw.get("point_number", raw.get("PointNumber", 1)),
        northing=raw.get("northing", raw.get("Northing", 0.0)),
        easting=raw.get("easting", raw.get("Easting", 0.0)),
        elevation=raw.get("elevation", raw.get("Elevation", 0.0)),
        code=measurement_display_code(base_code, control_code),
        figure_id=raw.get("figure_id", raw.get("FigureId", "")),
        base_code=base_code,
        control_code=control_code,
        control_parameter=control_parameter,
        fit_type=raw.get("fit_type", raw.get("FitType", "PlaneLeastSquares")),
        fit_residual=raw.get("fit_residual", raw.get("FitResidual", 0.0)),
        source_cloud_id=raw.get("source_cloud_id", raw.get("SourceCloudId", "")),
        created_at=raw.get("created_at", raw.get("CreatedAt", "")),
        notes=raw.get("notes", raw.get("Notes", "")),
        measurement_source=raw.get("measurement_source", raw.get("MeasurementSource", "DerivedSurface")),
        surface_id=raw.get("surface_id", raw.get("SurfaceId", "")),
        surface_tile_id=raw.get("surface_tile_id", raw.get("SurfaceTileId", "")),
        surface_method=raw.get("surface_method", raw.get("SurfaceMethod", "")),
    )


def figure_segment_from_dict(raw: dict) -> FigureSegmentRecord:
    return FigureSegmentRecord(
        segment_id=raw.get("segment_id", raw.get("SegmentId", str(uuid.uuid4()))),
        segment_kind=raw.get("segment_kind", raw.get("SegmentKind", "Line")),
        code=raw.get("code", raw.get("Code", "")),
        layer_name=raw.get("layer_name", raw.get("LayerName", raw.get("code", raw.get("Code", "")))),
        control_code=raw.get("control_code", raw.get("ControlCode", "")),
        control_parameter=raw.get("control_parameter", raw.get("ControlParameter", "")),
        created_by_point_number=raw.get("created_by_point_number", raw.get("CreatedByPointNumber", 0)),
        point_numbers=list(raw.get("point_numbers", raw.get("PointNumbers", []))),
        survey_points=[tuple(point) for point in raw.get("survey_points", raw.get("SurveyPoints", []))],
    )


def backfill_code_metadata(project: CSTopoProject) -> None:
    builtins = load_builtin_code_palette()
    if not builtins:
        return

    project.code_palette = list(builtins)
    palette_by_code = {style.code.upper(): style for style in project.code_palette}
    if project.active_code.upper() not in palette_by_code:
        project.active_code = project.code_palette[0].code if project.code_palette else ""

    for figure in project.figures:
        style = palette_by_code.get(figure.code.upper())
        if style is not None:
            figure.layer_name = style.layer_name
            figure.style = style
        else:
            figure.layer_name = figure.code
            figure.style = CodeStyle(
                code=figure.code,
                layer_name=figure.code,
                display_name=figure.code,
                point_type="Point (no triangulation)",
                visible=False,
            )


def _shot_point(shot: ShotRecord) -> Point3:
    return (shot.northing, shot.easting, shot.elevation)


def _point_number_from_parameter(parameter: str) -> Optional[int]:
    text = parameter.strip().upper()
    if text.startswith("P"):
        text = text[1:]
    return int(text) if text.isdigit() else None


def _sample_circle_points(center_n: float, center_e: float, radius: float, z: float, count: int = 49) -> List[Point3]:
    points: List[Point3] = []
    for index in range(count):
        angle = 2.0 * math.pi * index / (count - 1)
        points.append((center_n + math.sin(angle) * radius, center_e + math.cos(angle) * radius, z))
    return points


def _circle_from_three_points(a: ShotRecord, b: ShotRecord, c: ShotRecord) -> Optional[List[Point3]]:
    ax, ay = a.easting, a.northing
    bx, by = b.easting, b.northing
    cx, cy = c.easting, c.northing
    d = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by))
    if abs(d) < 1.0e-9:
        return None
    ux = ((ax * ax + ay * ay) * (by - cy) + (bx * bx + by * by) * (cy - ay) + (cx * cx + cy * cy) * (ay - by)) / d
    uy = ((ax * ax + ay * ay) * (cx - bx) + (bx * bx + by * by) * (ax - cx) + (cx * cx + cy * cy) * (bx - ax)) / d
    radius = math.hypot(ax - ux, ay - uy)
    return _sample_circle_points(uy, ux, radius, c.elevation)


def _rectangle_points(corner: ShotRecord, previous: Optional[ShotRecord], parameter: str, shots_by_number: Dict[int, ShotRecord]) -> List[Point3]:
    if parameter.upper().startswith("P"):
        opposite = shots_by_number.get(_point_number_from_parameter(parameter) or -1)
        if opposite is None:
            return []
        return [
            _shot_point(corner),
            (corner.northing, opposite.easting, corner.elevation),
            _shot_point(opposite),
            (opposite.northing, corner.easting, opposite.elevation),
            _shot_point(corner),
        ]
    if previous is None:
        return []
    try:
        width = float(parameter)
    except ValueError:
        return []
    dn = corner.northing - previous.northing
    de = corner.easting - previous.easting
    length = math.hypot(dn, de)
    if length <= 1.0e-9:
        return []
    pn = -de / length * width
    pe = dn / length * width
    return [
        _shot_point(previous),
        _shot_point(corner),
        (corner.northing + pn, corner.easting + pe, corner.elevation),
        (previous.northing + pn, previous.easting + pe, previous.elevation),
        _shot_point(previous),
    ]


def build_figure_segments(project: CSTopoProject) -> List[FigureSegmentRecord]:
    segments: List[FigureSegmentRecord] = []
    shots_by_number = {shot.point_number: shot for shot in project.shots}
    shots_by_base: Dict[str, List[ShotRecord]] = {}
    for shot in project.shots:
        shots_by_base.setdefault((shot.base_code or shot.code).upper(), []).append(shot)

    def add_segment(kind: str, code: str, point_numbers: List[int], points: List[Point3], control_code: str = "", parameter: str = "", created_by: int = 0) -> None:
        if len(points) < 2:
            return
        style = project.style_for(code)
        segments.append(
            FigureSegmentRecord(
                segment_id=str(uuid.uuid4()),
                segment_kind=kind,
                code=code,
                layer_name=style.layer_name,
                control_code=control_code,
                control_parameter=parameter,
                created_by_point_number=created_by,
                point_numbers=point_numbers,
                survey_points=points,
            )
        )

    for figure in project.figures:
        sequence = [number for number in figure.point_numbers if number in shots_by_number]
        for a_number, b_number in zip(sequence, sequence[1:]):
            add_segment("Line", figure.code, [a_number, b_number], [_shot_point(shots_by_number[a_number]), _shot_point(shots_by_number[b_number])])
        if figure.loop_closed and len(sequence) >= 2:
            add_segment("Line", figure.code, [sequence[-1], sequence[0]], [_shot_point(shots_by_number[sequence[-1]]), _shot_point(shots_by_number[sequence[0]])], "CLS", "", sequence[-1])

    for shot in project.shots:
        base_code = (shot.base_code or shot.code).upper()
        base_shots = shots_by_base.get(base_code, [])
        index = base_shots.index(shot) if shot in base_shots else -1
        previous = base_shots[index - 1] if index > 0 else None
        control = shot.control_code.upper()
        if control == "JPT":
            target = shots_by_number.get(_point_number_from_parameter(shot.control_parameter) or -1)
            if target is not None:
                add_segment("JoinLine", base_code, [target.point_number, shot.point_number], [_shot_point(target), _shot_point(shot)], control, shot.control_parameter, shot.point_number)
        elif control in {"OH", "OV"} and previous is not None:
            try:
                distance = float(shot.control_parameter)
            except ValueError:
                continue
            if control == "OH":
                dn = shot.northing - previous.northing
                de = shot.easting - previous.easting
                length = math.hypot(dn, de)
                if length <= 1.0e-9:
                    continue
                offset_n = -de / length * distance
                offset_e = dn / length * distance
                points = [
                    (previous.northing + offset_n, previous.easting + offset_e, previous.elevation),
                    (shot.northing + offset_n, shot.easting + offset_e, shot.elevation),
                ]
            else:
                points = [
                    (previous.northing, previous.easting, previous.elevation + distance),
                    (shot.northing, shot.easting, shot.elevation + distance),
                ]
            add_segment("OffsetLine", base_code, [previous.point_number, shot.point_number], points, control, shot.control_parameter, shot.point_number)
        elif control in {"PT", "NPT", "SCE"} and index >= 2:
            circle = _circle_from_three_points(base_shots[index - 2], base_shots[index - 1], shot)
            if circle is not None:
                add_segment("Circle" if control == "SCE" else "Arc", base_code, [base_shots[index - 2].point_number, base_shots[index - 1].point_number, shot.point_number], circle, control, shot.control_parameter, shot.point_number)
        elif control in {"SCPT", "ESC"} and index >= 2:
            curve_points = [_shot_point(candidate) for candidate in base_shots[max(0, index - 3) : index + 1]]
            add_segment("SmoothCurve", base_code, [candidate.point_number for candidate in base_shots[max(0, index - 3) : index + 1]], curve_points, control, shot.control_parameter, shot.point_number)
        elif control == "RECT":
            points = _rectangle_points(shot, previous, shot.control_parameter, shots_by_number)
            add_segment("Rectangle", base_code, [point.point_number for point in [previous, shot] if point is not None], points, control, shot.control_parameter, shot.point_number)
        elif control == "SCR":
            try:
                radius = float(shot.control_parameter) if shot.control_parameter else 0.0
            except ValueError:
                radius = 0.0
            if radius <= 0.0 and index + 1 < len(base_shots):
                next_shot = base_shots[index + 1]
                radius = math.hypot(next_shot.northing - shot.northing, next_shot.easting - shot.easting)
            if radius > 0.0:
                add_segment("Circle", base_code, [shot.point_number], _sample_circle_points(shot.northing, shot.easting, radius, shot.elevation), control, shot.control_parameter, shot.point_number)

    return segments


def project_from_dict(raw: dict) -> CSTopoProject:
    project = CSTopoProject(
        schema_version="1.4",
        project_name=raw.get("project_name", raw.get("projectName", "Untitled CSTopo Project")),
        active_code=raw.get("active_code", raw.get("activeCode", "")),
        active_point_cloud_id=raw.get("active_point_cloud_id", raw.get("activePointCloudId", "")),
        next_point_number=raw.get("next_point_number", raw.get("nextPointNumber", 1)),
        navigation_mode=raw.get("navigation_mode", raw.get("navigationMode", "Walk")),
        point_clouds=[PointCloudSource(**item) for item in raw.get("point_clouds", raw.get("pointClouds", []))],
        cache_manifest_path=raw.get("cache_manifest_path", raw.get("cacheManifestPath", "")),
        runtime_streaming=RuntimeStreamingSettings(**raw.get("runtime_streaming", raw.get("runtimeStreaming", {}))),
        surface_settings=SurfaceSettings(**raw.get("surface_settings", raw.get("surfaceSettings", {}))),
        derived_surfaces=[derived_surface_from_dict(item) for item in raw.get("derived_surfaces", raw.get("derivedSurfaces", []))],
        code_palette=[code_style_from_dict(item) for item in raw.get("code_palette", raw.get("codePalette", []))],
        shots=[shot_from_dict(item) for item in raw.get("shots", [])],
        figures=[],
        figure_segments=[figure_segment_from_dict(item) for item in raw.get("figure_segments", raw.get("figureSegments", []))],
    )
    for item in raw.get("figures", []):
        style_raw = item.get("style") or {"code": item["code"], "layer_name": item.get("layer_name", item["code"])}
        project.figures.append(
            FigureRecord(
                figure_id=item["figure_id"],
                code=item["code"],
                layer_name=item.get("layer_name", item["code"]),
                point_numbers=list(item.get("point_numbers", [])),
                closed=bool(item.get("closed", False)),
                loop_closed=bool(item.get("loop_closed", item.get("bLoopClosed", item.get("loopClosed", False)))),
                style=code_style_from_dict(style_raw),
            )
        )
    backfill_code_metadata(project)
    project.rebuild_figure_segments()
    return project


def fit_plane_elevation(samples: Sequence[Point3], northing: float, easting: float) -> Tuple[float, float]:
    if len(samples) < 3:
        raise ValueError("At least three samples are required for a fitted surface shot.")

    # The plane is z = a*northing + b*easting + c. Solve normal equations.
    sx = sy = sz = sxx = sxy = syy = sxz = syz = 0.0
    for x, y, z in samples:
        sx += x
        sy += y
        sz += z
        sxx += x * x
        sxy += x * y
        syy += y * y
        sxz += x * z
        syz += y * z

    matrix = [
        [sxx, sxy, sx, sxz],
        [sxy, syy, sy, syz],
        [sx, sy, float(len(samples)), sz],
    ]
    a, b, c = solve_3x3(matrix)
    residual = math.sqrt(sum(((a * x + b * y + c) - z) ** 2 for x, y, z in samples) / len(samples))
    return a * northing + b * easting + c, residual


def solve_3x3(matrix: List[List[float]]) -> Tuple[float, float, float]:
    for col in range(3):
        pivot = max(range(col, 3), key=lambda row: abs(matrix[row][col]))
        if abs(matrix[pivot][col]) < 1e-12:
            raise ValueError("Sample points do not define a stable fitted plane.")
        if pivot != col:
            matrix[col], matrix[pivot] = matrix[pivot], matrix[col]
        divisor = matrix[col][col]
        for idx in range(col, 4):
            matrix[col][idx] /= divisor
        for row in range(3):
            if row == col:
                continue
            factor = matrix[row][col]
            for idx in range(col, 4):
                matrix[row][idx] -= factor * matrix[col][idx]
    return matrix[0][3], matrix[1][3], matrix[2][3]


def read_las_header(source_path: Path) -> LasHeaderInfo:
    with source_path.open("rb") as handle:
        header = handle.read(375)

    if len(header) < 227 or header[:4] != b"LASF":
        raise ValueError(f"{source_path} is not a valid LAS/LAZ file with a readable LAS header.")

    version_major = header[24]
    version_minor = header[25]
    header_size = struct.unpack_from("<H", header, 94)[0]
    point_data_offset = struct.unpack_from("<I", header, 96)[0]
    vlr_count = struct.unpack_from("<I", header, 100)[0]
    point_format = header[104] & 0x3F
    point_record_length = struct.unpack_from("<H", header, 105)[0]
    point_count = struct.unpack_from("<I", header, 107)[0]

    if version_major == 1 and version_minor >= 4 and len(header) >= 255:
        extended_count = struct.unpack_from("<Q", header, 247)[0]
        if extended_count:
            point_count = extended_count

    scale = struct.unpack_from("<ddd", header, 131)
    offset = struct.unpack_from("<ddd", header, 155)
    max_x, min_x, max_y, min_y, max_z, min_z = struct.unpack_from("<dddddd", header, 179)
    coordinate_system_wkt = ""
    linear_unit_name = "unknown"
    linear_unit_to_meters = 0.0

    with source_path.open("rb") as handle:
        header_and_vlrs = handle.read(min(point_data_offset, 1024 * 1024))

    vlr_offset = header_size
    for _ in range(vlr_count):
        if vlr_offset + 54 > len(header_and_vlrs):
            break
        user_id = header_and_vlrs[vlr_offset + 2 : vlr_offset + 18].split(b"\0", 1)[0].decode("ascii", errors="ignore")
        record_id = struct.unpack_from("<H", header_and_vlrs, vlr_offset + 18)[0]
        record_length = struct.unpack_from("<H", header_and_vlrs, vlr_offset + 20)[0]
        record_data_offset = vlr_offset + 54
        if record_data_offset + record_length > len(header_and_vlrs):
            break
        if user_id.upper() == "LASF_PROJECTION" and record_id in {2111, 2112}:
            coordinate_system_wkt = (
                header_and_vlrs[record_data_offset : record_data_offset + record_length]
                .split(b"\0", 1)[0]
                .decode("ascii", errors="ignore")
            )
            unit_marker = 'UNIT["'
            unit_start = coordinate_system_wkt.upper().rfind(unit_marker)
            if unit_start >= 0:
                name_start = unit_start + len(unit_marker)
                name_end = coordinate_system_wkt.find('"', name_start)
                value_start = coordinate_system_wkt.find(",", name_end)
                value_end = coordinate_system_wkt.find(",", value_start + 1)
                if value_end < 0:
                    value_end = coordinate_system_wkt.find("]", value_start + 1)
                if name_end > name_start and value_start > 0 and value_end > value_start:
                    linear_unit_name = coordinate_system_wkt[name_start:name_end]
                    linear_unit_to_meters = float(coordinate_system_wkt[value_start + 1 : value_end])
        vlr_offset = record_data_offset + record_length

    return LasHeaderInfo(
        version=f"{version_major}.{version_minor}",
        header_size=header_size,
        point_data_offset=point_data_offset,
        vlr_count=vlr_count,
        point_format=point_format,
        point_record_length=point_record_length,
        point_count=point_count,
        scale=scale,
        offset=offset,
        bounds_min=(min_x, min_y, min_z),
        bounds_max=(max_x, max_y, max_z),
        coordinate_system_wkt=coordinate_system_wkt,
        linear_unit_name=linear_unit_name,
        linear_unit_to_meters=linear_unit_to_meters,
    )


def create_source_record(source_path: Path, cache_dir: Path, target_cache_format: str = "COPC") -> PointCloudSource:
    suffix = source_path.suffix.lower()
    if suffix not in {".las", ".laz"}:
        raise ValueError("CSTopo imports .las, .laz, and .copc.laz files.")
    is_copc = source_path.name.lower().endswith(".copc.laz")
    cache_path = source_path if is_copc else cache_dir / f"{source_path.stem}.copc.laz"
    header = read_las_header(source_path)
    record = PointCloudSource(
        source_id=str(uuid.uuid4()),
        source_path=str(source_path),
        cache_path=str(cache_path),
        cache_format="COPC" if is_copc else target_cache_format,
        point_count=header.point_count,
        bounds_min=header.bounds_min,
        bounds_max=header.bounds_max,
        coordinate_system_wkt=header.coordinate_system_wkt,
        linear_unit_name=header.linear_unit_name,
        linear_unit_to_meters=header.linear_unit_to_meters,
    )
    record.direct_open_eligible = record.point_count <= record.direct_open_point_threshold
    refresh_source_runtime_state(record)
    return record


def build_pdal_copc_command(source_path: Path, cache_dir: Path) -> List[str]:
    output_path = cache_dir / f"{source_path.stem}.copc.laz"
    return [find_pdal_executable(), "translate", str(source_path), str(output_path), "--writer", "writers.copc"]


def find_pdal_executable() -> str:
    configured = os.environ.get("CSTOPO_PDAL_PATH")
    if configured and Path(configured).exists():
        return configured
    on_path = shutil.which("pdal")
    if on_path:
        return on_path
    if DEFAULT_QGIS_PDAL.exists():
        return str(DEFAULT_QGIS_PDAL)
    raise FileNotFoundError(
        "PDAL was not found. Add pdal.exe to PATH, set CSTOPO_PDAL_PATH, or install QGIS with PDAL."
    )


def translate_to_copc(source_path: Path, cache_dir: Path) -> Path:
    cache_dir.mkdir(parents=True, exist_ok=True)
    output_path = cache_dir / f"{source_path.stem}.copc.laz"
    subprocess.run(build_pdal_copc_command(source_path, cache_dir), check=True)
    return output_path


def build_default_cache_manifest_path(project_path: Path) -> Path:
    return project_path.with_name(f"{project_path.stem}.cachemanifest.json")


def build_default_runtime_window_path(cache_dir: Path, source_id: str) -> Path:
    return cache_dir / "runtime" / f"{source_id[:8]}_window.las"


def compute_runtime_window_sample_spacing(window_radius_source_units: float, target_point_budget: int) -> float:
    safe_radius = max(window_radius_source_units, 1.0)
    safe_budget = max(target_point_budget, 1)
    window_area = (safe_radius * 2.0) ** 2
    return max(0.15, min(math.sqrt(window_area / safe_budget), safe_radius))


def _timestamp_or_empty(path: Path) -> str:
    return datetime.fromtimestamp(path.stat().st_mtime, tz=timezone.utc).isoformat() if path.exists() else ""


def refresh_source_runtime_state(source: PointCloudSource) -> PointCloudSource:
    source_path = Path(source.source_path)
    cache_path = Path(source.cache_path) if source.cache_path else Path()
    source.source_exists = source_path.exists()
    source.cache_exists = bool(source.cache_path) and cache_path.exists()
    source.source_modified_at = _timestamp_or_empty(source_path)
    source.cache_modified_at = _timestamp_or_empty(cache_path) if source.cache_path else ""
    source.direct_open_eligible = source.point_count <= source.direct_open_point_threshold or source.point_count <= 0

    if source.cache_format == "DirectLasLaz":
        source.cache_preferred_for_runtime = False
        source.cache_out_of_date = False
        source.runtime_display_path = source.source_path if source.source_exists else ""
        source.cache_state = "Ready" if source.source_exists else "Missing"
        source.cache_status = (
            f"Using direct source path: {source.source_path}"
            if source.source_exists
            else f"Source missing: {source.source_path}"
        )
        return source

    if source.source_path.lower() == source.cache_path.lower() and source.cache_exists:
        source.cache_preferred_for_runtime = True
        source.cache_out_of_date = False
        source.runtime_display_path = source.cache_path
        source.cache_state = "Ready"
        source.cache_status = f"Using source COPC cache: {source.cache_path}"
        return source

    source.cache_out_of_date = False
    if source.source_modified_at and source.cache_modified_at:
        source.cache_out_of_date = source.source_modified_at > source.cache_modified_at

    if source.cache_exists:
        source.cache_state = "Stale" if source.cache_out_of_date else "Ready"
    else:
        source.cache_state = "Missing"

    can_use_source_directly = source.source_exists and source.direct_open_eligible
    source.cache_preferred_for_runtime = source.cache_exists and (not source.cache_out_of_date or not can_use_source_directly)
    source.runtime_display_path = source.cache_path if source.cache_preferred_for_runtime else (source.source_path if source.source_exists else "")

    if not source.source_exists and not source.cache_exists:
        source.cache_state = "Missing"
        source.runtime_display_path = ""
        source.cache_status = f"Source and cache are missing for {source.source_id}"
        return source

    if source.cache_preferred_for_runtime:
        source.cache_status = f"Using cache: {source.cache_path}"
    elif source.source_exists:
        reason = "Source is direct-open eligible." if source.direct_open_eligible else "Source is being used until cache is built."
        source.cache_status = f"{reason} Runtime path: {source.source_path}"
    else:
        source.cache_status = f"Cache missing: {source.cache_path}"
    return source


def build_cache_manifest(project: CSTopoProject, project_path: Path) -> PointCloudCacheManifest:
    entries = [refresh_source_runtime_state(PointCloudSource(**asdict(source))) for source in project.point_clouds]
    return PointCloudCacheManifest(
        generated_at=datetime.now(timezone.utc).isoformat(),
        project_path=str(project_path),
        entries=entries,
    )


def build_copc_window_pipeline(
    source: PointCloudSource,
    output_path: Path,
    center_x: float,
    center_y: float,
    window_radius_source_units: float,
    sample_spacing_source_units: float,
    vertical_padding_source_units: float,
) -> list:
    zmin = source.bounds_min[2] - vertical_padding_source_units
    zmax = source.bounds_max[2] + vertical_padding_source_units
    bounds = (
        f"([{center_x - window_radius_source_units:.3f},{center_x + window_radius_source_units:.3f}],"
        f"[{center_y - window_radius_source_units:.3f},{center_y + window_radius_source_units:.3f}],"
        f"[{zmin:.3f},{zmax:.3f}])"
    )
    return [
        {
            "type": "readers.copc",
            "filename": source.cache_path.replace("\\", "/"),
            "bounds": bounds,
        },
        {
            "type": "filters.sample",
            "radius": sample_spacing_source_units,
        },
        {
            "type": "writers.las",
            "filename": str(output_path).replace("\\", "/"),
        },
    ]


def extract_runtime_window(
    source: PointCloudSource,
    cache_dir: Path,
    center_x: float,
    center_y: float,
    settings: RuntimeStreamingSettings,
) -> Path:
    output_path = build_default_runtime_window_path(cache_dir, source.source_id)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    pipeline_path = output_path.with_suffix(".json")
    sample_spacing = compute_runtime_window_sample_spacing(
        settings.window_radius_source_units,
        settings.target_point_budget,
    )
    pipeline = build_copc_window_pipeline(
        source,
        output_path,
        center_x,
        center_y,
        settings.window_radius_source_units,
        sample_spacing,
        settings.vertical_padding_source_units,
    )
    pipeline_path.write_text(json.dumps(pipeline, indent=2), encoding="utf-8")
    try:
        subprocess.run([find_pdal_executable(), "pipeline", str(pipeline_path)], check=True)
    finally:
        if pipeline_path.exists():
            pipeline_path.unlink()
    return output_path


def apply_cache_manifest(project: CSTopoProject, manifest: PointCloudCacheManifest) -> None:
    by_id = {entry.source_id: entry for entry in manifest.entries}
    updated: List[PointCloudSource] = []
    for source in project.point_clouds:
        manifest_entry = by_id.get(source.source_id)
        updated.append(manifest_entry if manifest_entry is not None else refresh_source_runtime_state(source))
    project.point_clouds = updated


def read_pdal_info(source_path: Path) -> dict:
    result = subprocess.run(
        [find_pdal_executable(), "info", "--summary", str(source_path)],
        check=True,
        capture_output=True,
        text=True,
    )
    return json.loads(result.stdout)


def read_pdal_stats(source_path: Path) -> dict:
    result = subprocess.run(
        [find_pdal_executable(), "info", "--stats", str(source_path)],
        check=True,
        capture_output=True,
        text=True,
    )
    return json.loads(result.stdout)


def build_default_surface_manifest_path(cache_dir: Path, source_id: str) -> Path:
    return cache_dir / "surfaces" / source_id[:8] / "surface_manifest.json"


def write_surface_build_progress(
    progress_path: Optional[Path],
    progress: float,
    stage: str,
    message: str,
    current: int = 0,
    total: int = 0,
) -> None:
    if progress_path is None:
        return

    progress_path.parent.mkdir(parents=True, exist_ok=True)
    clamped_progress = max(0.0, min(1.0, progress))
    payload = {
        "progress": clamped_progress,
        "stage": stage,
        "message": message,
        "current": current,
        "total": total,
        "updated_at": datetime.now(timezone.utc).isoformat(),
    }
    temp_path = progress_path.with_suffix(progress_path.suffix + ".tmp")
    temp_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    temp_path.replace(progress_path)


def _classification_support(stats: dict) -> Tuple[bool, bool]:
    statistics = stats.get("stats", {}).get("statistic", [])
    for item in statistics:
        if item.get("name") != "Classification":
            continue
        minimum = item.get("minimum")
        maximum = item.get("maximum")
        if minimum is None or maximum is None:
            return True, False
        return True, float(minimum) <= 2.0 <= float(maximum)
    return False, False


def _build_ground_pipeline(source: PointCloudSource, output_path: Path, classification_method: str) -> list:
    reader_type = "readers.copc" if source.source_path.lower().endswith(".copc.laz") else "readers.las"
    pipeline = [{"type": reader_type, "filename": source.source_path.replace("\\", "/")}]
    stats = read_pdal_stats(Path(source.source_path))
    has_classification, has_ground = _classification_support(stats)
    if has_classification and has_ground:
        ground_source = "existingClassification"
        pipeline.append({"type": "filters.range", "limits": "Classification[2:2]"})
    else:
        ground_source = "pdalClassified"
        if classification_method.upper() == "PMF":
            pipeline.append({"type": "filters.pmf"})
        else:
            pipeline.append({"type": "filters.smrf"})
        pipeline.append({"type": "filters.range", "limits": "Classification[2:2]"})
    pipeline.append({"type": "writers.las", "filename": str(output_path).replace("\\", "/")})
    return pipeline, ground_source


def _read_las_xyz_points(source_path: Path) -> List[Point3]:
    header = read_las_header(source_path)
    if header.point_record_length < 12:
        raise ValueError(f"{source_path} does not contain readable XYZ point records.")

    points: List[Point3] = []
    with source_path.open("rb") as handle:
        handle.seek(header.point_data_offset)
        for _ in range(header.point_count):
            record = handle.read(header.point_record_length)
            if len(record) < 12:
                break
            ix, iy, iz = struct.unpack_from("<iii", record, 0)
            x = ix * header.scale[0] + header.offset[0]
            y = iy * header.scale[1] + header.offset[1]
            z = iz * header.scale[2] + header.offset[2]
            points.append((x, y, z))
    return points


def _fill_height_grid(
    heights: List[float],
    valid: List[bool],
    support_distance: List[int],
    cols: int,
    rows: int,
    max_distance_cells: int = 2,
) -> None:
    if max_distance_cells <= 0:
        return

    for _ in range(max_distance_cells):
        changed = False
        next_heights = list(heights)
        next_valid = list(valid)
        next_support_distance = list(support_distance)
        for row in range(rows):
            for col in range(cols):
                index = row * cols + col
                if valid[index]:
                    continue
                neighbors: List[Tuple[float, int]] = []
                for drow in (-1, 0, 1):
                    for dcol in (-1, 0, 1):
                        if drow == 0 and dcol == 0:
                            continue
                        nrow = row + drow
                        ncol = col + dcol
                        if 0 <= nrow < rows and 0 <= ncol < cols:
                            nindex = nrow * cols + ncol
                            if valid[nindex] and support_distance[nindex] < max_distance_cells:
                                neighbors.append((heights[nindex], support_distance[nindex]))
                if len(neighbors) >= 3:
                    next_heights[index] = sum(height for height, _ in neighbors) / len(neighbors)
                    next_valid[index] = True
                    next_support_distance[index] = min(distance for _, distance in neighbors) + 1
                    changed = True
        heights[:] = next_heights
        valid[:] = next_valid
        support_distance[:] = next_support_distance
        if not changed:
            break


def _interpolate_point(a: Point3, b: Point3, t: float) -> Point3:
    return (
        a[0] + (b[0] - a[0]) * t,
        a[1] + (b[1] - a[1]) * t,
        a[2] + (b[2] - a[2]) * t,
    )


def _clip_polygon_axis(points: List[Point3], axis: int, limit: float, keep_less_equal: bool) -> List[Point3]:
    if not points:
        return []

    def inside(point: Point3) -> bool:
        return point[axis] <= limit if keep_less_equal else point[axis] >= limit

    clipped: List[Point3] = []
    previous = points[-1]
    previous_inside = inside(previous)
    for current in points:
        current_inside = inside(current)
        if current_inside != previous_inside:
            denominator = current[axis] - previous[axis]
            if abs(denominator) > 1.0e-12:
                clipped.append(_interpolate_point(previous, current, (limit - previous[axis]) / denominator))
        if current_inside:
            clipped.append(current)
        previous = current
        previous_inside = current_inside
    return clipped


def _clip_polygon_to_rect(points: List[Point3], min_x: float, min_y: float, max_x: float, max_y: float) -> List[Point3]:
    clipped = _clip_polygon_axis(points, 0, min_x, False)
    clipped = _clip_polygon_axis(clipped, 0, max_x, True)
    clipped = _clip_polygon_axis(clipped, 1, min_y, False)
    return _clip_polygon_axis(clipped, 1, max_y, True)


def _polygon_area_xy(points: Sequence[Point3]) -> float:
    if len(points) < 3:
        return 0.0
    area = 0.0
    previous = points[-1]
    for current in points:
        area += (previous[0] * current[1]) - (current[0] * previous[1])
        previous = current
    return abs(area) * 0.5


def _build_surface_tile(
    tile_id: str,
    points: Sequence[Point3],
    sample_min_x: float,
    sample_min_y: float,
    sample_max_x: float,
    sample_max_y: float,
    core_min_x: float,
    core_min_y: float,
    core_max_x: float,
    core_max_y: float,
    cell_size: float,
    output_path: Path,
    max_fill_cells: int = 2,
    max_triangle_z_delta: Optional[float] = None,
) -> Optional[SurfaceTileRecord]:
    if core_max_x <= core_min_x or core_max_y <= core_min_y:
        return None

    cols = max(2, int(math.ceil((sample_max_x - sample_min_x) / cell_size)) + 1)
    rows = max(2, int(math.ceil((sample_max_y - sample_min_y) / cell_size)) + 1)
    sums = [0.0] * (cols * rows)
    counts = [0] * (cols * rows)

    for x, y, z in points:
        if x < sample_min_x or x > sample_max_x or y < sample_min_y or y > sample_max_y:
            continue
        col = min(max(int((x - sample_min_x) / cell_size), 0), cols - 1)
        row = min(max(int((y - sample_min_y) / cell_size), 0), rows - 1)
        index = row * cols + col
        sums[index] += z
        counts[index] += 1

    heights = [0.0] * (cols * rows)
    valid = [False] * (cols * rows)
    support_distance = [1_000_000] * (cols * rows)
    for index, count in enumerate(counts):
        if count > 0:
            heights[index] = sums[index] / count
            valid[index] = True
            support_distance[index] = 0

    _fill_height_grid(heights, valid, support_distance, cols, rows, max_fill_cells)
    if sum(1 for item in valid if item) < 4:
        return None

    vertices: List[Point3] = []
    indices: Dict[int, int] = {}
    for row in range(rows):
        for col in range(cols):
            index = row * cols + col
            if not valid[index]:
                continue
            indices[index] = len(vertices)
            vertices.append((sample_min_x + col * cell_size, sample_min_y + row * cell_size, heights[index]))

    output_vertices: List[Point3] = []
    output_indices: Dict[Tuple[float, float, float], int] = {}
    triangles: List[int] = []

    def output_index(point: Point3) -> int:
        key = (round(point[0], 8), round(point[1], 8), round(point[2], 8))
        existing = output_indices.get(key)
        if existing is not None:
            return existing
        output_indices[key] = len(output_vertices)
        output_vertices.append(point)
        return output_indices[key]

    for row in range(rows - 1):
        for col in range(cols - 1):
            i00 = row * cols + col
            i10 = row * cols + (col + 1)
            i01 = (row + 1) * cols + col
            i11 = (row + 1) * cols + (col + 1)
            if not (valid[i00] and valid[i10] and valid[i01] and valid[i11]):
                continue
            if max(support_distance[i00], support_distance[i10], support_distance[i01], support_distance[i11]) > max_fill_cells:
                continue
            z00 = heights[i00]
            z10 = heights[i10]
            z01 = heights[i01]
            z11 = heights[i11]
            if max_triangle_z_delta is not None and max(z00, z10, z01, z11) - min(z00, z10, z01, z11) > max_triangle_z_delta:
                continue
            if abs(z00 - z11) <= abs(z10 - z01):
                cell_triangles = (
                    (vertices[indices[i00]], vertices[indices[i10]], vertices[indices[i11]]),
                    (vertices[indices[i00]], vertices[indices[i11]], vertices[indices[i01]]),
                )
            else:
                cell_triangles = (
                    (vertices[indices[i00]], vertices[indices[i10]], vertices[indices[i01]]),
                    (vertices[indices[i10]], vertices[indices[i11]], vertices[indices[i01]]),
                )

            for triangle in cell_triangles:
                clipped = _clip_polygon_to_rect(list(triangle), core_min_x, core_min_y, core_max_x, core_max_y)
                if _polygon_area_xy(clipped) <= 1.0e-8:
                    continue
                for index in range(1, len(clipped) - 1):
                    triangle_points = (clipped[0], clipped[index], clipped[index + 1])
                    if _polygon_area_xy(triangle_points) <= 1.0e-8:
                        continue
                    triangles.extend(output_index(point) for point in triangle_points)

    if len(triangles) < 3:
        return None

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        json.dumps(
            {
                "tile_id": tile_id,
                "bounds_min": [min(vertex[0] for vertex in output_vertices), min(vertex[1] for vertex in output_vertices), min(vertex[2] for vertex in output_vertices)],
                "bounds_max": [max(vertex[0] for vertex in output_vertices), max(vertex[1] for vertex in output_vertices), max(vertex[2] for vertex in output_vertices)],
                "vertices": output_vertices,
                "triangles": triangles,
            },
            indent=2,
        ),
        encoding="utf-8",
    )
    return SurfaceTileRecord(
        tile_id=tile_id,
        bounds_min=(min(vertex[0] for vertex in output_vertices), min(vertex[1] for vertex in output_vertices), min(vertex[2] for vertex in output_vertices)),
        bounds_max=(max(vertex[0] for vertex in output_vertices), max(vertex[1] for vertex in output_vertices), max(vertex[2] for vertex in output_vertices)),
        vertex_count=len(output_vertices),
        triangle_count=len(triangles) // 3,
        mesh_path=str(output_path),
        status="Ready",
    )


def _point_key(point: Point3, precision: float = SOURCE_POINT_PRECISION) -> Tuple[int, int, int]:
    return (
        int(round(point[0] / precision)),
        int(round(point[1] / precision)),
        int(round(point[2] / precision)),
    )


def _xy_key(x: float, y: float, precision: float = SOURCE_POINT_PRECISION) -> Tuple[int, int]:
    return (int(round(x / precision)), int(round(y / precision)))


def _dedupe_xy_points(points: Sequence[Point3]) -> List[Point3]:
    grouped: Dict[Tuple[int, int], Tuple[float, float, float, int]] = {}
    for x, y, z in points:
        key = _xy_key(x, y)
        if key in grouped:
            gx, gy, gz, count = grouped[key]
            grouped[key] = (gx, gy, gz + z, count + 1)
        else:
            grouped[key] = (x, y, z, 1)
    return [(x, y, zsum / count) for x, y, zsum, count in grouped.values()]


@dataclass
class TinBoundaryDiagnostics:
    input_triangle_count: int = 0
    retained_triangle_count: int = 0
    rejected_large_triangle_count: int = 0
    rejected_z_delta_count: int = 0
    rejected_bounds_count: int = 0
    boundary_edge_count: int = 0
    boundary_loop_count: int = 0
    resolved_max_edge_length: float = 0.0
    resolved_max_area: float = 0.0
    constrained_triangulation_status: str = "NotRun"

    def merge(self, other: "TinBoundaryDiagnostics") -> None:
        self.input_triangle_count += other.input_triangle_count
        self.retained_triangle_count += other.retained_triangle_count
        self.rejected_large_triangle_count += other.rejected_large_triangle_count
        self.rejected_z_delta_count += other.rejected_z_delta_count
        self.rejected_bounds_count += other.rejected_bounds_count
        self.boundary_edge_count += other.boundary_edge_count
        self.boundary_loop_count += other.boundary_loop_count
        self.resolved_max_edge_length = max(self.resolved_max_edge_length, other.resolved_max_edge_length)
        self.resolved_max_area = max(self.resolved_max_area, other.resolved_max_area)
        if other.constrained_triangulation_status != "NotRun":
            if self.constrained_triangulation_status == "NotRun":
                self.constrained_triangulation_status = other.constrained_triangulation_status
            elif self.constrained_triangulation_status != other.constrained_triangulation_status:
                self.constrained_triangulation_status = "Mixed"


def _edge_lengths_xy(triangle_points: Sequence[Point3]) -> Tuple[float, float, float]:
    return (
        math.dist((triangle_points[0][0], triangle_points[0][1]), (triangle_points[1][0], triangle_points[1][1])),
        math.dist((triangle_points[1][0], triangle_points[1][1]), (triangle_points[2][0], triangle_points[2][1])),
        math.dist((triangle_points[2][0], triangle_points[2][1]), (triangle_points[0][0], triangle_points[0][1])),
    )


def _triangle_circumradius_xy(triangle_points: Sequence[Point3]) -> float:
    a, b, c = _edge_lengths_xy(triangle_points)
    area = _polygon_area_xy(triangle_points)
    if area <= 1.0e-12:
        return float("inf")
    return (a * b * c) / (4.0 * area)


def _estimate_tin_point_spacing(points: Sequence[Point3]) -> float:
    if len(points) < 2:
        return SOURCE_POINT_PRECISION

    min_x = min(point[0] for point in points)
    max_x = max(point[0] for point in points)
    min_y = min(point[1] for point in points)
    max_y = max(point[1] for point in points)
    area = max((max_x - min_x) * (max_y - min_y), SOURCE_POINT_PRECISION * SOURCE_POINT_PRECISION)
    density_spacing = max(math.sqrt(area / len(points)), SOURCE_POINT_PRECISION)

    sample_count = min(len(points), 512)
    if len(points) == sample_count:
        sample = list(points)
    else:
        stride = (len(points) - 1) / float(sample_count - 1)
        sample = [points[min(int(round(index * stride)), len(points) - 1)] for index in range(sample_count)]

    nearest: List[float] = []
    for index, point in enumerate(sample):
        best = float("inf")
        px, py, _ = point
        for other_index, other in enumerate(sample):
            if other_index == index:
                continue
            distance = math.dist((px, py), (other[0], other[1]))
            if SOURCE_POINT_PRECISION < distance < best:
                best = distance
        if math.isfinite(best):
            nearest.append(best)

    if nearest:
        nearest.sort()
        return max(min(nearest[len(nearest) // 2], density_spacing * 3.0), SOURCE_POINT_PRECISION)

    return density_spacing


def _resolve_tin_max_edge_length(
    points: Sequence[Point3],
    configured_edge_length: float,
    edge_multiplier: float,
) -> float:
    if configured_edge_length > 0.0:
        return configured_edge_length
    spacing = _estimate_tin_point_spacing(points)
    return max(spacing * max(edge_multiplier, 1.0), SOURCE_POINT_PRECISION * 4.0)


def _tin_triangle_owner(
    triangle_points: Sequence[Point3],
    source_min_x: float,
    source_min_y: float,
    source_max_x: float,
    source_max_y: float,
    tile_size: float,
    max_tile_x: int,
    max_tile_y: int,
) -> Optional[Tuple[int, int]]:
    centroid_x = sum(point[0] for point in triangle_points) / 3.0
    centroid_y = sum(point[1] for point in triangle_points) / 3.0
    if centroid_x < source_min_x or centroid_x > source_max_x or centroid_y < source_min_y or centroid_y > source_max_y:
        return None
    return _owner_tile_for_xy(centroid_x, centroid_y, source_min_x, source_min_y, tile_size, max_tile_x, max_tile_y)


def _tin_triangle_rejection_reason(
    triangle_points: Sequence[Point3],
    source_min_x: float,
    source_min_y: float,
    source_max_x: float,
    source_max_y: float,
    tile_size: float,
    max_tile_x: int,
    max_tile_y: int,
    max_edge_length: float,
    max_area: float,
    max_triangle_z_delta: Optional[float],
) -> Optional[str]:
    if len(triangle_points) != 3 or _polygon_area_xy(triangle_points) <= 1.0e-8:
        return "large"
    if _tin_triangle_owner(triangle_points, source_min_x, source_min_y, source_max_x, source_max_y, tile_size, max_tile_x, max_tile_y) is None:
        return "bounds"

    edge_lengths = _edge_lengths_xy(triangle_points)
    area = _polygon_area_xy(triangle_points)
    if max_edge_length > 0.0 and max(edge_lengths) > max_edge_length:
        return "large"
    if max_area > 0.0 and area > max_area:
        return "large"
    if max_edge_length > 0.0 and _triangle_circumradius_xy(triangle_points) > max_edge_length:
        return "large"
    if max_triangle_z_delta is not None:
        elevations = [point[2] for point in triangle_points]
        if max(elevations) - min(elevations) > max_triangle_z_delta:
            return "z_delta"
    return None


def _extract_tin_boundary_edges(triangles: Sequence[Tuple[Point3, Point3, Point3]]) -> List[Tuple[Tuple[int, int, int], Tuple[int, int, int]]]:
    edge_counts: Dict[Tuple[Tuple[int, int, int], Tuple[int, int, int]], int] = {}
    for triangle_points in triangles:
        keys = [_point_key(point) for point in triangle_points]
        for index, start in enumerate(keys):
            end = keys[(index + 1) % 3]
            edge = tuple(sorted((start, end)))
            edge_counts[edge] = edge_counts.get(edge, 0) + 1
    return [edge for edge, count in edge_counts.items() if count == 1]


def _count_tin_boundary_loops(boundary_edges: Sequence[Tuple[Tuple[int, int, int], Tuple[int, int, int]]]) -> int:
    adjacency: Dict[Tuple[int, int, int], List[Tuple[int, int, int]]] = {}
    for start, end in boundary_edges:
        adjacency.setdefault(start, []).append(end)
        adjacency.setdefault(end, []).append(start)

    visited: Set[Tuple[int, int, int]] = set()
    loop_count = 0
    for node in adjacency:
        if node in visited:
            continue
        stack = [node]
        component: Set[Tuple[int, int, int]] = set()
        while stack:
            current = stack.pop()
            if current in component:
                continue
            component.add(current)
            stack.extend(adjacency.get(current, []))
        visited.update(component)
        if component and all(len(adjacency.get(item, [])) == 2 for item in component):
            loop_count += 1
    return loop_count


def _triangulate_tin_points(points: Sequence[Point3], boundary_edges: Optional[Sequence[Tuple[Tuple[int, int, int], Tuple[int, int, int]]]] = None) -> Tuple[List[Tuple[Point3, Point3, Point3]], str]:
    try:
        import numpy as np
        import triangle as triangle_lib
    except ImportError as exc:
        raise RuntimeError("The triangle and numpy Python packages are required for BufferedDelaunayTIN surface builds.") from exc

    vertices_2d = np.array([(point[0], point[1]) for point in points], dtype=float)
    z_by_xy = {_xy_key(point[0], point[1]): point[2] for point in points}
    triangulation_input = {"vertices": vertices_2d}
    options = "Q"
    if boundary_edges:
        index_by_point_key = {_point_key(point): index for index, point in enumerate(points)}
        segments = []
        for start_key, end_key in boundary_edges:
            start_index = index_by_point_key.get(start_key)
            end_index = index_by_point_key.get(end_key)
            if start_index is not None and end_index is not None and start_index != end_index:
                segments.append((start_index, end_index))
        if segments:
            triangulation_input["segments"] = np.array(segments, dtype=int)
            options = "pQ"

    try:
        triangulated = triangle_lib.triangulate(triangulation_input, options)
    except Exception:
        if boundary_edges:
            return [], "Failed"
        raise

    raw_triangles = triangulated.get("triangles")
    result_vertices = triangulated.get("vertices")
    if raw_triangles is None or result_vertices is None:
        return [], "NoTriangles"

    triangles: List[Tuple[Point3, Point3, Point3]] = []
    for triangle_indices in raw_triangles:
        triangle_points: List[Point3] = []
        for vertex_index in triangle_indices:
            x = float(result_vertices[int(vertex_index)][0])
            y = float(result_vertices[int(vertex_index)][1])
            z = z_by_xy.get(_xy_key(x, y))
            if z is None:
                triangle_points = []
                break
            triangle_points.append((x, y, z))
        if len(triangle_points) == 3:
            triangles.append((triangle_points[0], triangle_points[1], triangle_points[2]))
    return triangles, "Constrained" if boundary_edges else "RawDelaunay"


def _filter_tin_triangles(
    raw_triangles: Sequence[Tuple[Point3, Point3, Point3]],
    source_min_x: float,
    source_min_y: float,
    source_max_x: float,
    source_max_y: float,
    tile_size: float,
    max_tile_x: int,
    max_tile_y: int,
    max_edge_length: float,
    area_multiplier: float,
    max_triangle_z_delta: Optional[float],
) -> Tuple[List[Tuple[Point3, Point3, Point3]], TinBoundaryDiagnostics]:
    diagnostics = TinBoundaryDiagnostics(
        input_triangle_count=len(raw_triangles),
        resolved_max_edge_length=max_edge_length,
        resolved_max_area=(max_edge_length * max_edge_length * max(area_multiplier, 0.0)) if max_edge_length > 0.0 else 0.0,
    )
    retained: List[Tuple[Point3, Point3, Point3]] = []
    for triangle_points in raw_triangles:
        reason = _tin_triangle_rejection_reason(
            triangle_points,
            source_min_x,
            source_min_y,
            source_max_x,
            source_max_y,
            tile_size,
            max_tile_x,
            max_tile_y,
            max_edge_length,
            diagnostics.resolved_max_area,
            max_triangle_z_delta,
        )
        if reason is None:
            retained.append(triangle_points)
        elif reason == "bounds":
            diagnostics.rejected_bounds_count += 1
        elif reason == "z_delta":
            diagnostics.rejected_z_delta_count += 1
        else:
            diagnostics.rejected_large_triangle_count += 1

    diagnostics.retained_triangle_count = len(retained)
    boundary_edges = _extract_tin_boundary_edges(retained)
    diagnostics.boundary_edge_count = len(boundary_edges)
    diagnostics.boundary_loop_count = _count_tin_boundary_loops(boundary_edges)
    return retained, diagnostics


def _build_supported_tin_triangles(
    points: Sequence[Point3],
    source_min_x: float,
    source_min_y: float,
    source_max_x: float,
    source_max_y: float,
    tile_size: float,
    max_tile_x: int,
    max_tile_y: int,
    configured_edge_length: float,
    edge_multiplier: float,
    area_multiplier: float,
    max_triangle_z_delta: Optional[float],
    constrain_to_boundary: bool,
) -> Tuple[List[Tuple[Point3, Point3, Point3]], TinBoundaryDiagnostics]:
    max_edge_length = _resolve_tin_max_edge_length(points, configured_edge_length, edge_multiplier)
    raw_triangles, _ = _triangulate_tin_points(points)
    retained, diagnostics = _filter_tin_triangles(
        raw_triangles,
        source_min_x,
        source_min_y,
        source_max_x,
        source_max_y,
        tile_size,
        max_tile_x,
        max_tile_y,
        max_edge_length,
        area_multiplier,
        max_triangle_z_delta,
    )
    boundary_edges = _extract_tin_boundary_edges(retained)
    if not constrain_to_boundary or not boundary_edges or diagnostics.boundary_loop_count <= 0:
        diagnostics.constrained_triangulation_status = "PostTrimNoConstraint"
        return retained, diagnostics

    constrained_raw, status = _triangulate_tin_points(points, boundary_edges)
    if not constrained_raw:
        diagnostics.constrained_triangulation_status = f"{status};PostTrimFallback"
        return retained, diagnostics

    constrained_retained, constrained_diagnostics = _filter_tin_triangles(
        constrained_raw,
        source_min_x,
        source_min_y,
        source_max_x,
        source_max_y,
        tile_size,
        max_tile_x,
        max_tile_y,
        max_edge_length,
        area_multiplier,
        max_triangle_z_delta,
    )
    if constrained_retained:
        constrained_diagnostics.rejected_large_triangle_count += diagnostics.rejected_large_triangle_count
        constrained_diagnostics.rejected_z_delta_count += diagnostics.rejected_z_delta_count
        constrained_diagnostics.rejected_bounds_count += diagnostics.rejected_bounds_count
        constrained_diagnostics.constrained_triangulation_status = "Constrained"
        return constrained_retained, constrained_diagnostics

    diagnostics.constrained_triangulation_status = "ConstrainedRejectedAll;PostTrimFallback"
    return retained, diagnostics


def _decimate_tin_points(points: Sequence[Point3], max_points: int, mode: str) -> Tuple[List[Point3], str]:
    if max_points <= 0 or len(points) <= max_points:
        return list(points), "none"

    min_x = min(point[0] for point in points)
    max_x = max(point[0] for point in points)
    min_y = min(point[1] for point in points)
    max_y = max(point[1] for point in points)
    area = max((max_x - min_x) * (max_y - min_y), 1.0)
    bucket_size = max(math.sqrt(area / max_points), SOURCE_POINT_PRECISION)
    selected: List[Point3] = []

    for _ in range(6):
        buckets: Dict[Tuple[int, int], List[Point3]] = {}
        for point in points:
            bx = int(math.floor((point[0] - min_x) / bucket_size))
            by = int(math.floor((point[1] - min_y) / bucket_size))
            buckets.setdefault((bx, by), []).append(point)

        selected_keys: Set[Tuple[int, int, int]] = set()
        selected = []
        for bucket_points in buckets.values():
            low = min(bucket_points, key=lambda item: item[2])
            high = max(bucket_points, key=lambda item: item[2])
            cx = sum(item[0] for item in bucket_points) / len(bucket_points)
            cy = sum(item[1] for item in bucket_points) / len(bucket_points)
            center = min(bucket_points, key=lambda item: (item[0] - cx) ** 2 + (item[1] - cy) ** 2)
            for point in (low, high, center):
                key = _point_key(point)
                if key not in selected_keys:
                    selected_keys.add(key)
                    selected.append(point)

        if len(selected) <= max_points:
            break
        bucket_size *= math.sqrt(len(selected) / max_points) * 1.1

    if len(selected) > max_points:
        selected = sorted(selected, key=_point_key)
        stride = len(selected) / max_points
        selected = [selected[min(int(index * stride), len(selected) - 1)] for index in range(max_points)]

    return selected, f"{mode}:{len(points)}->{len(selected)}"


def _owner_tile_for_xy(
    x: float,
    y: float,
    min_x: float,
    min_y: float,
    tile_size: float,
    max_tile_x: int,
    max_tile_y: int,
) -> Tuple[int, int]:
    tile_x = min(max(int(math.floor((x - min_x) / tile_size)), 0), max_tile_x)
    tile_y = min(max(int(math.floor((y - min_y) / tile_size)), 0), max_tile_y)
    return tile_x, tile_y


def _triangle_line_interval(triangle_points: Sequence[Point3], axis: int, value: float) -> Optional[Tuple[float, float]]:
    other_axis = 1 - axis
    intersections: List[float] = []
    for index, a in enumerate(triangle_points):
        b = triangle_points[(index + 1) % len(triangle_points)]
        da = a[axis] - value
        db = b[axis] - value
        if abs(da) <= 1.0e-8 and abs(db) <= 1.0e-8:
            intersections.extend([a[other_axis], b[other_axis]])
        elif da * db <= 0.0 and abs(da - db) > 1.0e-12:
            t = -da / (db - da)
            if -1.0e-8 <= t <= 1.0 + 1.0e-8:
                intersections.append(a[other_axis] + (b[other_axis] - a[other_axis]) * t)
    if len(intersections) < 2:
        return None
    return min(intersections), max(intersections)


def _merge_intervals(intervals: Sequence[Tuple[float, float]], tolerance: float) -> List[Tuple[float, float]]:
    merged: List[Tuple[float, float]] = []
    for start, end in sorted(intervals):
        if end < start:
            start, end = end, start
        if not merged or start > merged[-1][1] + tolerance:
            merged.append((start, end))
        else:
            merged[-1] = (merged[-1][0], max(merged[-1][1], end))
    return merged


def _audit_tin_seams(
    exported_triangles: Sequence[Tuple[Point3, Point3, Point3]],
    populated_tiles: Set[Tuple[int, int]],
    min_x: float,
    min_y: float,
    max_x: float,
    max_y: float,
    tile_size: float,
    max_tile_x: int,
    max_tile_y: int,
    duplicate_triangle_count: int,
    tolerance: float,
    support_points: Optional[Sequence[Point3]] = None,
) -> Tuple[str, str]:
    if duplicate_triangle_count > 0:
        return "Failed", f"Duplicate owned TIN triangles detected: {duplicate_triangle_count}."

    vertical_intervals: Dict[int, List[Tuple[float, float]]] = {index: [] for index in range(1, max_tile_x + 1)}
    horizontal_intervals: Dict[int, List[Tuple[float, float]]] = {index: [] for index in range(1, max_tile_y + 1)}
    for triangle_points in exported_triangles:
        tri_min_x = min(point[0] for point in triangle_points)
        tri_max_x = max(point[0] for point in triangle_points)
        tri_min_y = min(point[1] for point in triangle_points)
        tri_max_y = max(point[1] for point in triangle_points)
        first_x_line = max(1, int(math.floor((tri_min_x - min_x) / tile_size)))
        last_x_line = min(max_tile_x, int(math.ceil((tri_max_x - min_x) / tile_size)))
        for line_index in range(first_x_line, last_x_line + 1):
            line_x = min_x + line_index * tile_size
            if tri_min_x - tolerance <= line_x <= tri_max_x + tolerance:
                interval = _triangle_line_interval(triangle_points, 0, line_x)
                if interval is not None:
                    vertical_intervals.setdefault(line_index, []).append(interval)

        first_y_line = max(1, int(math.floor((tri_min_y - min_y) / tile_size)))
        last_y_line = min(max_tile_y, int(math.ceil((tri_max_y - min_y) / tile_size)))
        for line_index in range(first_y_line, last_y_line + 1):
            line_y = min_y + line_index * tile_size
            if tri_min_y - tolerance <= line_y <= tri_max_y + tolerance:
                interval = _triangle_line_interval(triangle_points, 1, line_y)
                if interval is not None:
                    horizontal_intervals.setdefault(line_index, []).append(interval)

    def supported_spans(axis: int, line_value: float, span_start: float, span_end: float) -> List[Tuple[float, float]]:
        if support_points is None:
            return [(span_start, span_end)]
        other_axis = 1 - axis
        negative_values: List[float] = []
        positive_values: List[float] = []
        for point in support_points:
            value = point[other_axis]
            if value < span_start or value > span_end:
                continue
            delta = point[axis] - line_value
            if -tolerance <= delta <= 0.0:
                negative_values.append(value)
            if 0.0 <= delta <= tolerance:
                positive_values.append(value)

        def spans_from_values(values: Sequence[float]) -> List[Tuple[float, float]]:
            if not values:
                return []
            spans: List[Tuple[float, float]] = []
            sorted_values = sorted(values)
            start = sorted_values[0]
            end = sorted_values[0]
            for value in sorted_values[1:]:
                if value - end <= tolerance:
                    end = value
                else:
                    if end > start:
                        spans.append((start, end))
                    start = value
                    end = value
            if end > start:
                spans.append((start, end))
            return spans

        expected: List[Tuple[float, float]] = []
        for neg_start, neg_end in spans_from_values(negative_values):
            for pos_start, pos_end in spans_from_values(positive_values):
                overlap_start = max(neg_start, pos_start)
                overlap_end = min(neg_end, pos_end)
                if overlap_end - overlap_start > tolerance:
                    expected.append((overlap_start, overlap_end))
        return _merge_intervals(expected, tolerance)

    gap_messages: List[str] = []
    for line_index, intervals in vertical_intervals.items():
        line_x = min_x + line_index * tile_size
        for tile_y in range(max_tile_y + 1):
            if (line_index - 1, tile_y) not in populated_tiles or (line_index, tile_y) not in populated_tiles:
                continue
            tile_span_start = min_y + tile_y * tile_size
            tile_span_end = min(max_y, min_y + (tile_y + 1) * tile_size)
            for span_start, span_end in supported_spans(0, line_x, tile_span_start, tile_span_end):
                clipped = [(max(start, span_start), min(end, span_end)) for start, end in intervals if end >= span_start and start <= span_end]
                merged = _merge_intervals(clipped, tolerance)
                cursor = span_start
                for start, end in merged:
                    if start - cursor > tolerance:
                        gap_messages.append(f"x={line_x:.3f} y={cursor:.3f}-{start:.3f}")
                        break
                    cursor = max(cursor, end)
                if span_end - cursor > tolerance:
                    gap_messages.append(f"x={line_x:.3f} y={cursor:.3f}-{span_end:.3f}")

    for line_index, intervals in horizontal_intervals.items():
        line_y = min_y + line_index * tile_size
        for tile_x in range(max_tile_x + 1):
            if (tile_x, line_index - 1) not in populated_tiles or (tile_x, line_index) not in populated_tiles:
                continue
            tile_span_start = min_x + tile_x * tile_size
            tile_span_end = min(max_x, min_x + (tile_x + 1) * tile_size)
            for span_start, span_end in supported_spans(1, line_y, tile_span_start, tile_span_end):
                clipped = [(max(start, span_start), min(end, span_end)) for start, end in intervals if end >= span_start and start <= span_end]
                merged = _merge_intervals(clipped, tolerance)
                cursor = span_start
                for start, end in merged:
                    if start - cursor > tolerance:
                        gap_messages.append(f"y={line_y:.3f} x={cursor:.3f}-{start:.3f}")
                        break
                    cursor = max(cursor, end)
                if span_end - cursor > tolerance:
                    gap_messages.append(f"y={line_y:.3f} x={cursor:.3f}-{span_end:.3f}")

    if gap_messages:
        return "Failed", "TIN seam audit found uncovered seam spans: " + "; ".join(gap_messages[:8])
    return "Passed", "TIN seam audit passed: no duplicate owned triangles or uncovered populated seams."


def _build_tin_surface_tile(
    tile_id: str,
    tile_x: int,
    tile_y: int,
    points: Sequence[Point3],
    source_min_x: float,
    source_min_y: float,
    source_max_x: float,
    source_max_y: float,
    tile_size: float,
    max_tile_x: int,
    max_tile_y: int,
    output_path: Path,
    max_edge_length: float,
    max_triangle_z_delta: Optional[float],
    max_buffered_points: int,
    decimation_mode: str,
    exported_triangle_keys: Set[Tuple[Tuple[int, int, int], Tuple[int, int, int], Tuple[int, int, int]]],
    exported_triangles: List[Tuple[Point3, Point3, Point3]],
    boundary_edge_multiplier: float = 8.0,
    boundary_area_multiplier: float = 0.75,
    constrain_to_boundary: bool = True,
) -> Tuple[Optional[SurfaceTileRecord], int]:
    unique_points = _dedupe_xy_points(points)
    input_point_count = len(unique_points)
    used_points, decimation_method = _decimate_tin_points(unique_points, max_buffered_points, decimation_mode)
    if len(used_points) < 3:
        return None, 0

    supported_triangles, _ = _build_supported_tin_triangles(
        used_points,
        source_min_x,
        source_min_y,
        source_max_x,
        source_max_y,
        tile_size,
        max_tile_x,
        max_tile_y,
        configured_edge_length=max_edge_length,
        edge_multiplier=boundary_edge_multiplier,
        area_multiplier=boundary_area_multiplier,
        max_triangle_z_delta=max_triangle_z_delta,
        constrain_to_boundary=constrain_to_boundary,
    )
    if not supported_triangles:
        return None, 0

    output_vertices: List[Point3] = []
    output_indices: Dict[Tuple[int, int, int], int] = {}
    output_triangles: List[int] = []
    duplicate_triangle_count = 0

    def output_index(point: Point3) -> int:
        key = _point_key(point)
        existing = output_indices.get(key)
        if existing is not None:
            return existing
        output_indices[key] = len(output_vertices)
        output_vertices.append(point)
        return output_indices[key]

    for triangle_points in supported_triangles:
        owner_tile = _tin_triangle_owner(triangle_points, source_min_x, source_min_y, source_max_x, source_max_y, tile_size, max_tile_x, max_tile_y)
        if owner_tile != (tile_x, tile_y):
            continue

        triangle_key = tuple(sorted(_point_key(point) for point in triangle_points))
        if triangle_key in exported_triangle_keys:
            duplicate_triangle_count += 1
            continue
        exported_triangle_keys.add(triangle_key)
        exported_triangles.append((triangle_points[0], triangle_points[1], triangle_points[2]))
        output_triangles.extend(output_index(point) for point in triangle_points)

    if len(output_triangles) < 3:
        return None, duplicate_triangle_count

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        json.dumps(
            {
                "tile_id": tile_id,
                "bounds_min": [min(vertex[0] for vertex in output_vertices), min(vertex[1] for vertex in output_vertices), min(vertex[2] for vertex in output_vertices)],
                "bounds_max": [max(vertex[0] for vertex in output_vertices), max(vertex[1] for vertex in output_vertices), max(vertex[2] for vertex in output_vertices)],
                "vertices": output_vertices,
                "triangles": output_triangles,
            },
            indent=2,
        ),
        encoding="utf-8",
    )

    return SurfaceTileRecord(
        tile_id=tile_id,
        bounds_min=(min(vertex[0] for vertex in output_vertices), min(vertex[1] for vertex in output_vertices), min(vertex[2] for vertex in output_vertices)),
        bounds_max=(max(vertex[0] for vertex in output_vertices), max(vertex[1] for vertex in output_vertices), max(vertex[2] for vertex in output_vertices)),
        vertex_count=len(output_vertices),
        triangle_count=len(output_triangles) // 3,
        mesh_path=str(output_path),
        status="Ready",
        input_point_count=input_point_count,
        used_point_count=len(used_points),
        decimation_method=decimation_method,
    ), duplicate_triangle_count


def _collect_tin_surface_tile_candidates(
    points: Sequence[Point3],
    source_min_x: float,
    source_min_y: float,
    source_max_x: float,
    source_max_y: float,
    tile_size: float,
    max_tile_x: int,
    max_tile_y: int,
    max_edge_length: float,
    boundary_edge_multiplier: float,
    boundary_area_multiplier: float,
    constrain_to_boundary: bool,
    max_triangle_z_delta: Optional[float],
    max_buffered_points: int,
    decimation_mode: str,
) -> Tuple[List[Tuple[Tuple[int, int], Tuple[Point3, Point3, Point3]]], int, int, str, TinBoundaryDiagnostics]:
    unique_points = _dedupe_xy_points(points)
    input_point_count = len(unique_points)
    used_points, decimation_method = _decimate_tin_points(unique_points, max_buffered_points, decimation_mode)
    if len(used_points) < 3:
        return [], input_point_count, len(used_points), decimation_method, TinBoundaryDiagnostics()

    supported_triangles, diagnostics = _build_supported_tin_triangles(
        used_points,
        source_min_x,
        source_min_y,
        source_max_x,
        source_max_y,
        tile_size,
        max_tile_x,
        max_tile_y,
        configured_edge_length=max_edge_length,
        edge_multiplier=boundary_edge_multiplier,
        area_multiplier=boundary_area_multiplier,
        max_triangle_z_delta=max_triangle_z_delta,
        constrain_to_boundary=constrain_to_boundary,
    )

    candidates: List[Tuple[Tuple[int, int], Tuple[Point3, Point3, Point3]]] = []
    for triangle_points in supported_triangles:
        owner_tile = _tin_triangle_owner(triangle_points, source_min_x, source_min_y, source_max_x, source_max_y, tile_size, max_tile_x, max_tile_y)
        if owner_tile is None:
            continue
        candidates.append((owner_tile, (triangle_points[0], triangle_points[1], triangle_points[2])))
    return candidates, input_point_count, len(used_points), decimation_method, diagnostics


def _write_tin_surface_tile(
    tile_id: str,
    owner_tile: Tuple[int, int],
    candidate_triangles: Sequence[Tuple[Point3, Point3, Point3]],
    input_point_count: int,
    used_point_count: int,
    decimation_method: str,
    output_path: Path,
    exported_triangle_keys: Set[Tuple[Tuple[int, int, int], Tuple[int, int, int], Tuple[int, int, int]]],
    exported_triangles: List[Tuple[Point3, Point3, Point3]],
) -> Tuple[Optional[SurfaceTileRecord], int]:
    output_vertices: List[Point3] = []
    output_indices: Dict[Tuple[int, int, int], int] = {}
    output_triangles: List[int] = []
    duplicate_triangle_count = 0

    def output_index(point: Point3) -> int:
        key = _point_key(point)
        existing = output_indices.get(key)
        if existing is not None:
            return existing
        output_indices[key] = len(output_vertices)
        output_vertices.append(point)
        return output_indices[key]

    for triangle_points in candidate_triangles:
        triangle_key = tuple(sorted(_point_key(point) for point in triangle_points))
        if triangle_key in exported_triangle_keys:
            duplicate_triangle_count += 1
            continue
        exported_triangle_keys.add(triangle_key)
        exported_triangles.append((triangle_points[0], triangle_points[1], triangle_points[2]))
        output_triangles.extend(output_index(point) for point in triangle_points)

    if len(output_triangles) < 3:
        return None, duplicate_triangle_count

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        json.dumps(
            {
                "tile_id": tile_id,
                "bounds_min": [min(vertex[0] for vertex in output_vertices), min(vertex[1] for vertex in output_vertices), min(vertex[2] for vertex in output_vertices)],
                "bounds_max": [max(vertex[0] for vertex in output_vertices), max(vertex[1] for vertex in output_vertices), max(vertex[2] for vertex in output_vertices)],
                "vertices": output_vertices,
                "triangles": output_triangles,
            },
            indent=2,
        ),
        encoding="utf-8",
    )
    return SurfaceTileRecord(
        tile_id=tile_id,
        bounds_min=(min(vertex[0] for vertex in output_vertices), min(vertex[1] for vertex in output_vertices), min(vertex[2] for vertex in output_vertices)),
        bounds_max=(max(vertex[0] for vertex in output_vertices), max(vertex[1] for vertex in output_vertices), max(vertex[2] for vertex in output_vertices)),
        vertex_count=len(output_vertices),
        triangle_count=len(output_triangles) // 3,
        mesh_path=str(output_path),
        status="Ready",
        input_point_count=input_point_count,
        used_point_count=used_point_count,
        decimation_method=decimation_method,
    ), duplicate_triangle_count


def _triangle_key(triangle_points: Sequence[Point3]) -> Tuple[Tuple[int, int, int], Tuple[int, int, int], Tuple[int, int, int]]:
    return tuple(sorted(_point_key(point) for point in triangle_points))


def _edge_key_for_points(a: Point3, b: Point3) -> Tuple[Tuple[int, int, int], Tuple[int, int, int]]:
    return tuple(sorted((_point_key(a), _point_key(b))))


def _boundary_loops_from_edges(
    boundary_edges: Sequence[Tuple[Tuple[int, int, int], Tuple[int, int, int]]]
) -> List[List[Tuple[int, int, int]]]:
    adjacency: Dict[Tuple[int, int, int], List[Tuple[int, int, int]]] = {}
    for start, end in boundary_edges:
        adjacency.setdefault(start, []).append(end)
        adjacency.setdefault(end, []).append(start)

    loops: List[List[Tuple[int, int, int]]] = []
    used_edges: Set[Tuple[Tuple[int, int, int], Tuple[int, int, int]]] = set()
    for edge_start, edge_end in sorted(boundary_edges):
        edge = tuple(sorted((edge_start, edge_end)))
        if edge in used_edges:
            continue
        loop = [edge_start]
        previous = edge_start
        current = edge_end
        used_edges.add(edge)
        while True:
            loop.append(current)
            if current == loop[0]:
                break
            next_candidates = [
                candidate
                for candidate in sorted(adjacency.get(current, []))
                if candidate != previous and tuple(sorted((current, candidate))) not in used_edges
            ]
            if not next_candidates:
                break
            next_node = next_candidates[0]
            used_edges.add(tuple(sorted((current, next_node))))
            previous, current = current, next_node
        if len(loop) >= 4 and loop[-1] == loop[0]:
            loops.append(loop)
    return loops


def _write_global_tin_surface_tile(
    tile_id: str,
    candidate_triangles: Sequence[Tuple[Point3, Point3, Point3]],
    boundary_edges: Sequence[Tuple[Tuple[int, int, int], Tuple[int, int, int]]],
    output_path: Path,
) -> Optional[SurfaceTileRecord]:
    output_vertices: List[Point3] = []
    output_indices: Dict[Tuple[int, int, int], int] = {}
    output_triangles: List[int] = []

    def output_index(point: Point3) -> int:
        key = _point_key(point)
        existing = output_indices.get(key)
        if existing is not None:
            return existing
        output_indices[key] = len(output_vertices)
        output_vertices.append(point)
        return output_indices[key]

    for triangle_points in candidate_triangles:
        output_triangles.extend(output_index(point) for point in triangle_points)

    if len(output_triangles) < 3:
        return None

    boundary_loops = _boundary_loops_from_edges(boundary_edges)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        json.dumps(
            {
                "tile_id": tile_id,
                "bounds_min": [min(vertex[0] for vertex in output_vertices), min(vertex[1] for vertex in output_vertices), min(vertex[2] for vertex in output_vertices)],
                "bounds_max": [max(vertex[0] for vertex in output_vertices), max(vertex[1] for vertex in output_vertices), max(vertex[2] for vertex in output_vertices)],
                "vertices": output_vertices,
                "triangles": output_triangles,
                "boundary_edges": [[list(start), list(end)] for start, end in sorted(boundary_edges)],
                "boundary_loops": [[list(point_key) for point_key in loop] for loop in boundary_loops],
            },
            indent=2,
        ),
        encoding="utf-8",
    )
    return SurfaceTileRecord(
        tile_id=tile_id,
        bounds_min=(min(vertex[0] for vertex in output_vertices), min(vertex[1] for vertex in output_vertices), min(vertex[2] for vertex in output_vertices)),
        bounds_max=(max(vertex[0] for vertex in output_vertices), max(vertex[1] for vertex in output_vertices), max(vertex[2] for vertex in output_vertices)),
        vertex_count=len(output_vertices),
        triangle_count=len(output_triangles) // 3,
        mesh_path=str(output_path),
        status="Ready",
        input_point_count=len(output_vertices),
        used_point_count=len(output_vertices),
        decimation_method="none",
        boundary_edge_count=len(boundary_edges),
        boundary_loop_count=len(boundary_loops),
    )


def _build_global_tin_tile_data(
    points: Sequence[Point3],
    source_min_x: float,
    source_min_y: float,
    source_max_x: float,
    source_max_y: float,
    tile_size: float,
    max_tile_x: int,
    max_tile_y: int,
    configured_edge_length: float,
    edge_multiplier: float,
    area_multiplier: float,
    max_triangle_z_delta: Optional[float],
) -> Tuple[
    Dict[Tuple[int, int], List[Tuple[Point3, Point3, Point3]]],
    Dict[Tuple[int, int], List[Tuple[Tuple[int, int, int], Tuple[int, int, int]]]],
    TinBoundaryDiagnostics,
    str,
    str,
]:
    unique_points = _dedupe_xy_points(points)
    max_edge_length = _resolve_tin_max_edge_length(unique_points, configured_edge_length, edge_multiplier)
    raw_triangles, triangulation_status = _triangulate_tin_points(unique_points)
    retained_triangles, diagnostics = _filter_tin_triangles(
        raw_triangles,
        source_min_x,
        source_min_y,
        source_max_x,
        source_max_y,
        tile_size,
        max_tile_x,
        max_tile_y,
        max_edge_length,
        area_multiplier,
        max_triangle_z_delta,
    )
    diagnostics.constrained_triangulation_status = triangulation_status

    triangles_by_owner: Dict[Tuple[int, int], List[Tuple[Point3, Point3, Point3]]] = {}
    edge_to_owners: Dict[Tuple[Tuple[int, int, int], Tuple[int, int, int]], Set[Tuple[int, int]]] = {}
    edge_use_count: Dict[Tuple[Tuple[int, int, int], Tuple[int, int, int]], int] = {}
    seen_triangle_keys: Set[Tuple[Tuple[int, int, int], Tuple[int, int, int], Tuple[int, int, int]]] = set()
    duplicate_triangle_count = 0
    ownerless_triangle_count = 0

    for triangle_points in retained_triangles:
        triangle_key = _triangle_key(triangle_points)
        if triangle_key in seen_triangle_keys:
            duplicate_triangle_count += 1
            continue
        seen_triangle_keys.add(triangle_key)
        owner = _tin_triangle_owner(triangle_points, source_min_x, source_min_y, source_max_x, source_max_y, tile_size, max_tile_x, max_tile_y)
        if owner is None:
            ownerless_triangle_count += 1
            continue
        triangles_by_owner.setdefault(owner, []).append(triangle_points)
        for index, point in enumerate(triangle_points):
            edge = _edge_key_for_points(point, triangle_points[(index + 1) % 3])
            edge_to_owners.setdefault(edge, set()).add(owner)
            edge_use_count[edge] = edge_use_count.get(edge, 0) + 1

    boundary_edges_by_owner: Dict[Tuple[int, int], List[Tuple[Tuple[int, int, int], Tuple[int, int, int]]]] = {}
    nonmanifold_edge_count = 0
    for edge, use_count in edge_use_count.items():
        owners = edge_to_owners.get(edge, set())
        if use_count > 2:
            nonmanifold_edge_count += 1
        if use_count == 1 or len(owners) > 1:
            for owner in owners:
                boundary_edges_by_owner.setdefault(owner, []).append(edge)

    for owner in triangles_by_owner:
        boundary_edges_by_owner.setdefault(owner, [])

    diagnostics.retained_triangle_count = sum(len(items) for items in triangles_by_owner.values())
    diagnostics.boundary_edge_count = sum(len(items) for items in boundary_edges_by_owner.values())
    diagnostics.boundary_loop_count = sum(len(_boundary_loops_from_edges(items)) for items in boundary_edges_by_owner.values())

    audit_errors: List[str] = []
    if duplicate_triangle_count:
        audit_errors.append(f"duplicate owned triangles={duplicate_triangle_count}")
    if ownerless_triangle_count:
        audit_errors.append(f"ownerless triangles={ownerless_triangle_count}")
    if nonmanifold_edge_count:
        audit_errors.append(f"non-manifold edges={nonmanifold_edge_count}")
    missing_reciprocal_edges = 0
    for edge, owners in edge_to_owners.items():
        if len(owners) > 1:
            for owner in owners:
                if edge not in boundary_edges_by_owner.get(owner, []):
                    missing_reciprocal_edges += 1
    if missing_reciprocal_edges:
        audit_errors.append(f"non-reciprocal shared boundary edges={missing_reciprocal_edges}")

    if audit_errors:
        return triangles_by_owner, boundary_edges_by_owner, diagnostics, "Failed", "; ".join(audit_errors)

    return (
        triangles_by_owner,
        boundary_edges_by_owner,
        diagnostics,
        "Passed",
        "Global TIN topology audit passed: every retained triangle is owned once, boundary edges are reciprocal, and no non-manifold edges were found.",
    )


def build_surface_cache(
    project: CSTopoProject,
    source_id: str,
    cache_dir: Path,
    force_rebuild: bool = False,
    progress_path: Optional[Path] = None,
) -> DerivedSurfaceManifest:
    source = next((item for item in project.point_clouds if item.source_id == source_id), None)
    if source is None:
        raise ValueError(f"Point cloud {source_id} was not found.")

    write_surface_build_progress(progress_path, 0.02, "Preparing", "Preparing surface build.")
    manifest_path = build_default_surface_manifest_path(cache_dir, source.source_id)
    if manifest_path.exists() and not force_rebuild:
        manifest = derived_surface_from_dict(json.loads(manifest_path.read_text(encoding="utf-8")))
        if manifest.schema_version == SURFACE_MANIFEST_SCHEMA_VERSION:
            write_surface_build_progress(progress_path, 1.0, "Ready", "Existing surface manifest is current.", 1, 1)
            return manifest

    surface_dir = manifest_path.parent
    surface_dir.mkdir(parents=True, exist_ok=True)
    tiles_dir = surface_dir / "tiles"
    if tiles_dir.exists():
        shutil.rmtree(tiles_dir)
    ground_path = surface_dir / "ground.las"
    pipeline_path = surface_dir / "ground_pipeline.json"
    write_surface_build_progress(progress_path, 0.08, "Classifying Ground", "Classifying ground points with PDAL.")
    pipeline, ground_source = _build_ground_pipeline(source, ground_path, project.surface_settings.ground_classification_method)
    pipeline_path.write_text(json.dumps(pipeline, indent=2), encoding="utf-8")
    try:
        subprocess.run([find_pdal_executable(), "pipeline", str(pipeline_path)], check=True)
    finally:
        if pipeline_path.exists():
            pipeline_path.unlink()

    write_surface_build_progress(progress_path, 0.22, "Reading Ground", "Reading ground-classified points.")
    ground_points = _read_las_xyz_points(ground_path)
    if not ground_points:
        raise ValueError("Ground-classified surface build returned no points.")

    min_x, min_y, min_z = source.bounds_min
    max_x, max_y, max_z = source.bounds_max
    area = max((max_x - min_x) * (max_y - min_y), 1.0)
    density = max(len(ground_points), 1)
    nominal_spacing = math.sqrt(area / density)
    tile_size = max(project.surface_settings.tile_size_source_units, nominal_spacing * 8.0)
    configured_z_delta = project.surface_settings.max_surface_triangle_z_delta_source_units
    max_triangle_z_delta = configured_z_delta if configured_z_delta > 0.0 else None
    max_edge_length = project.surface_settings.tin_max_edge_length_source_units
    boundary_edge_multiplier = max(project.surface_settings.tin_boundary_edge_multiplier, 1.0)
    boundary_area_multiplier = max(project.surface_settings.tin_boundary_area_multiplier, 0.0)
    global_tin_max_points = max(3, int(project.surface_settings.global_tin_max_points))

    max_tile_x = max(0, int(math.ceil((max_x - min_x) / tile_size)) - 1)
    max_tile_y = max(0, int(math.ceil((max_y - min_y) / tile_size)) - 1)
    unique_ground_points = _dedupe_xy_points(ground_points)
    write_surface_build_progress(
        progress_path,
        0.32,
        "Preparing TIN",
        f"Preparing {len(unique_ground_points)} unique ground points for TIN generation.",
        len(unique_ground_points),
        len(unique_ground_points),
    )
    if len(unique_ground_points) > global_tin_max_points:
        raise ValueError(
            f"GlobalDelaunayTIN requires {len(unique_ground_points)} unique ground points, "
            f"which exceeds GlobalTinMaxPoints={global_tin_max_points}. Increase the cap or use a smaller source."
        )

    write_surface_build_progress(
        progress_path,
        0.38,
        "Generating TIN",
        f"Generating global Delaunay TIN from {len(unique_ground_points)} ground points.",
        0,
        len(unique_ground_points),
    )
    candidate_triangles_by_owner, boundary_edges_by_owner, aggregate_diagnostics, topology_audit_status, topology_audit_message = _build_global_tin_tile_data(
        unique_ground_points,
        min_x,
        min_y,
        max_x,
        max_y,
        tile_size,
        max_tile_x,
        max_tile_y,
        configured_edge_length=max_edge_length,
        edge_multiplier=boundary_edge_multiplier,
        area_multiplier=boundary_area_multiplier,
        max_triangle_z_delta=max_triangle_z_delta,
    )

    tiles: List[SurfaceTileRecord] = []
    exported_triangles: List[Tuple[Point3, Point3, Point3]] = []
    sorted_owner_tiles = sorted(candidate_triangles_by_owner.items())
    total_owner_tiles = max(len(sorted_owner_tiles), 1)
    write_surface_build_progress(
        progress_path,
        0.72,
        "Writing Tiles",
        f"Writing {len(sorted_owner_tiles)} surface tile(s).",
        0,
        total_owner_tiles,
    )
    for tile_index, (owner_tile, candidate_triangles) in enumerate(sorted_owner_tiles, start=1):
        tile_x, tile_y = owner_tile
        tile_id = f"{source.source_id[:8]}_{tile_x}_{tile_y}"
        tile_path = surface_dir / "tiles" / f"{tile_id}.json"
        tile = _write_global_tin_surface_tile(
            tile_id,
            candidate_triangles,
            boundary_edges_by_owner.get(owner_tile, []),
            tile_path,
        )
        if tile is not None:
            tiles.append(tile)
            exported_triangles.extend(candidate_triangles)
        write_surface_build_progress(
            progress_path,
            0.72 + (0.20 * tile_index / total_owner_tiles),
            "Writing Tiles",
            f"Writing surface tile {tile_index} of {total_owner_tiles}.",
            tile_index,
            total_owner_tiles,
        )

    if not tiles:
        raise ValueError("Ground-classified global TIN surface build produced no valid triangles.")

    write_surface_build_progress(progress_path, 0.94, "Auditing", "Auditing TIN topology and tile boundaries.")
    seam_audit_status = topology_audit_status
    seam_audit_message = topology_audit_message
    if topology_audit_status == "Failed":
        manifest = DerivedSurfaceManifest(
            surface_id=source.surface_id or str(uuid.uuid4()),
            source_cloud_id=source.source_id,
            schema_version=SURFACE_MANIFEST_SCHEMA_VERSION,
            build_state="Failed",
            build_method=SURFACE_BUILD_METHOD_GLOBAL_TIN,
            ground_source=ground_source,
            tile_size=tile_size,
            tile_count=len(tiles),
            bounds_min=source.bounds_min,
            bounds_max=source.bounds_max,
            source_crs=source.coordinate_system_wkt,
            linear_unit_name=source.linear_unit_name,
            linear_unit_to_meters=source.linear_unit_to_meters,
            generated_at=datetime.now(timezone.utc).isoformat(),
            manifest_path=str(manifest_path),
            tiles=tiles,
            total_vertex_count=sum(tile.vertex_count for tile in tiles),
            total_triangle_count=sum(tile.triangle_count for tile in tiles),
            decimation_used=any(tile.decimation_method != "none" for tile in tiles),
            tin_boundary_mode=project.surface_settings.tin_boundary_mode,
            tin_resolved_max_edge_length_source_units=aggregate_diagnostics.resolved_max_edge_length,
            tin_boundary_edge_multiplier=boundary_edge_multiplier,
            tin_boundary_area_multiplier=boundary_area_multiplier,
            tin_rejected_large_triangle_count=aggregate_diagnostics.rejected_large_triangle_count,
            tin_retained_candidate_triangle_count=aggregate_diagnostics.retained_triangle_count,
            tin_boundary_edge_count=aggregate_diagnostics.boundary_edge_count,
            tin_boundary_loop_count=aggregate_diagnostics.boundary_loop_count,
            tin_constrained_triangulation_status=aggregate_diagnostics.constrained_triangulation_status,
            global_point_count=len(unique_ground_points),
            global_retained_triangle_count=aggregate_diagnostics.retained_triangle_count,
            global_rejected_triangle_count=aggregate_diagnostics.rejected_large_triangle_count + aggregate_diagnostics.rejected_z_delta_count + aggregate_diagnostics.rejected_bounds_count,
            topology_audit_status=topology_audit_status,
            topology_audit_message=topology_audit_message,
            surface_collision_mode=project.surface_settings.surface_collision_mode,
            seam_audit_status=seam_audit_status,
            seam_audit_message=seam_audit_message,
        )
        manifest_path.write_text(json.dumps(asdict(manifest), indent=2), encoding="utf-8")
        raise ValueError(topology_audit_message)

    write_surface_build_progress(progress_path, 0.98, "Saving", "Saving derived surface manifest.")
    manifest = DerivedSurfaceManifest(
        surface_id=source.surface_id or str(uuid.uuid4()),
        source_cloud_id=source.source_id,
        schema_version=SURFACE_MANIFEST_SCHEMA_VERSION,
        build_state="Ready",
        build_method=SURFACE_BUILD_METHOD_GLOBAL_TIN,
        ground_source=ground_source,
        tile_size=tile_size,
        tile_count=len(tiles),
        bounds_min=source.bounds_min,
        bounds_max=source.bounds_max,
        source_crs=source.coordinate_system_wkt,
        linear_unit_name=source.linear_unit_name,
        linear_unit_to_meters=source.linear_unit_to_meters,
        generated_at=datetime.now(timezone.utc).isoformat(),
        manifest_path=str(manifest_path),
        tiles=tiles,
        total_vertex_count=sum(tile.vertex_count for tile in tiles),
        total_triangle_count=sum(tile.triangle_count for tile in tiles),
        decimation_used=any(tile.decimation_method != "none" for tile in tiles),
        tin_boundary_mode=project.surface_settings.tin_boundary_mode,
        tin_resolved_max_edge_length_source_units=aggregate_diagnostics.resolved_max_edge_length,
        tin_boundary_edge_multiplier=boundary_edge_multiplier,
        tin_boundary_area_multiplier=boundary_area_multiplier,
        tin_rejected_large_triangle_count=aggregate_diagnostics.rejected_large_triangle_count,
        tin_retained_candidate_triangle_count=aggregate_diagnostics.retained_triangle_count,
        tin_boundary_edge_count=aggregate_diagnostics.boundary_edge_count,
        tin_boundary_loop_count=aggregate_diagnostics.boundary_loop_count,
        tin_constrained_triangulation_status=aggregate_diagnostics.constrained_triangulation_status,
        global_point_count=len(unique_ground_points),
        global_retained_triangle_count=aggregate_diagnostics.retained_triangle_count,
        global_rejected_triangle_count=aggregate_diagnostics.rejected_large_triangle_count + aggregate_diagnostics.rejected_z_delta_count + aggregate_diagnostics.rejected_bounds_count,
        topology_audit_status=topology_audit_status,
        topology_audit_message=topology_audit_message,
        surface_collision_mode=project.surface_settings.surface_collision_mode,
        seam_audit_status=seam_audit_status,
        seam_audit_message=seam_audit_message,
    )
    manifest_path.write_text(json.dumps(asdict(manifest), indent=2), encoding="utf-8")

    source.surface_id = manifest.surface_id
    source.surface_manifest_path = str(manifest_path)
    decimation_note = "decimated" if manifest.decimation_used else "all points"
    source.surface_status = (
        f"Surface ready: {len(tiles)} tile(s) | {manifest.build_method} | {ground_source} | "
        f"{manifest.total_vertex_count} vertices | {manifest.total_triangle_count} triangles | {decimation_note} | "
        f"TIN trim edge {manifest.tin_resolved_max_edge_length_source_units:.2f} | {manifest.surface_collision_mode}"
    )
    source.surface_build_state = "Ready"

    project.derived_surfaces = [item for item in project.derived_surfaces if item.source_cloud_id != source.source_id]
    project.derived_surfaces.append(manifest)
    write_surface_build_progress(progress_path, 1.0, "Ready", source.surface_status, len(tiles), len(tiles))
    return manifest


def export_csv(project: CSTopoProject, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["PointNumber", "Northing", "Easting", "Elevation", "Code", "FigureId", "Notes"])
        for shot in project.shots:
            writer.writerow(
                [
                    shot.point_number,
                    f"{shot.northing:.4f}",
                    f"{shot.easting:.4f}",
                    f"{shot.elevation:.4f}",
                    shot.code,
                    shot.figure_id,
                    shot.notes or f"fit={shot.fit_type} residual={shot.fit_residual:.4f}",
                ]
            )


def export_dxf(project: CSTopoProject, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    shot_by_number = {shot.point_number: shot for shot in project.shots}
    lines: List[str] = [
        "0", "SECTION", "2", "HEADER", "0", "ENDSEC",
        "0", "SECTION", "2", "TABLES", "0", "TABLE", "2", "LAYER",
    ]
    for style in project.code_palette:
        lines.extend(["0", "LAYER", "2", style.layer_name, "70", "0", "62", "7", "6", "CONTINUOUS"])
    lines.extend(["0", "ENDTAB", "0", "ENDSEC", "0", "SECTION", "2", "ENTITIES"])

    for shot in project.shots:
        layer = project.style_for(shot.base_code or shot.code).layer_name
        lines.extend(["0", "POINT", "8", layer, "10", f"{shot.easting:.4f}", "20", f"{shot.northing:.4f}", "30", f"{shot.elevation:.4f}"])

    if not project.figure_segments:
        project.rebuild_figure_segments()

    for segment in project.figure_segments:
        if len(segment.survey_points) < 2:
            continue
        style = project.style_for(segment.code)
        if not point_type_creates_figure_linework(style.point_type):
            continue
        layer = segment.layer_name or style.layer_name
        lines.extend(["0", "POLYLINE", "8", layer, "66", "1", "70", "8"])
        for northing, easting, elevation in segment.survey_points:
            lines.extend(["0", "VERTEX", "8", layer, "10", f"{easting:.4f}", "20", f"{northing:.4f}", "30", f"{elevation:.4f}"])
        lines.extend(["0", "SEQEND"])
    lines.extend(["0", "ENDSEC", "0", "EOF"])
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def synthetic_road_samples(northing: float, easting: float) -> List[Point3]:
    samples = []
    for dn, de in [(-1.0, -1.0), (-1.0, 1.0), (1.0, -1.0), (1.0, 1.0), (0.0, 0.0)]:
        n = northing + dn
        e = easting + de
        z = 100.0 + 0.015 * n - 0.01 * e
        samples.append((n, e, z))
    return samples


def create_sample_project() -> CSTopoProject:
    project = CSTopoProject.default("Road Corridor Demo")
    for offset in range(4):
        n = 5000.0 + offset * 25.0
        e = 1000.0
        project.add_fitted_shot(n, e, synthetic_road_samples(n, e), code="BLDPAD", source_cloud_id="demo-cloud")
    project.split_figure("BLDPAD")
    for offset in range(4):
        n = 5000.0 + offset * 25.0
        e = 1012.0
        project.add_fitted_shot(n, e, synthetic_road_samples(n, e), code="DECK", source_cloud_id="demo-cloud")
    return project
