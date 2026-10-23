# Pyramidstackmerge tool

Reorder and stack pyramid tiff file.

Usage: pyramidstackmerge -i <tiff-file>
```
OPTIONS:
  -h,     --help              Print this help message and exit
  -i,     --input PATH:FILE REQUIRED
                              The input tiff image
  -o,     --order PATH:FILE   User-defined order or ROIs in a txt file
  -s,     --suffix TEXT       Suffix for the newly generated files, default: '_reordered'
  -d,     --dry-run           Dry run flag: do not write to disk, just print the new layout
                              and target paths, default: not set
```

The layout will either be square or a user-defined rectangle by leaving empty lines between image layout rows in the order text file.
The ROIs listed in the order txt must correspond to the ROI names in the GeoJSON file that accompanies the provided pyramid tiff file.
