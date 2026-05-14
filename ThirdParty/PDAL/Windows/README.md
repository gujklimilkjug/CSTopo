# CSTopo Bundled PDAL Runtime

This folder is the expected Windows runtime location for CSTopo's bundled PDAL executable:

`ThirdParty/PDAL/Windows/bin/pdal.exe`

Populate it from a known-good QGIS/OSGeo4W install with:

```powershell
python scripts/vendor_pdal_runtime.py
```

Package the prepared folder for GitHub Releases with:

```powershell
python scripts/vendor_pdal_runtime.py --package-only --package-zip dist\cstopo-pdal-runtime-windows-2.10.1.zip
```

Fresh machines can restore the runtime from the tracked manifest with:

```powershell
python scripts/bootstrap_pdal_runtime.py
```

The copied binaries are intentionally ignored by Git so the repository does not grow by hundreds of megabytes. Packaged CSTopo builds stage whatever runtime files are present here, and the in-app PDAL prompt uses the bootstrap script when the runtime is missing.
