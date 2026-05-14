# CSTopo Bundled PDAL Runtime

This folder is the expected Windows runtime location for CSTopo's bundled PDAL executable:

`ThirdParty/PDAL/Windows/bin/pdal.exe`

Populate it from a known-good QGIS/OSGeo4W install with:

```powershell
python scripts/vendor_pdal_runtime.py
```

The copied binaries are intentionally ignored by Git so the repository does not grow by hundreds of megabytes. Packaged CSTopo builds stage whatever runtime files are present here.
