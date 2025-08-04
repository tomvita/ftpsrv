#!/bin/sh
set -e

echo "=== Building and Packaging for Nintendo Switch ==="

echo "--- Configuring project ---"
cmake --preset switch

echo "--- Building project ---"
cmake --build --preset switch

echo "--- Packaging release files ---"

# Clean previous switch output
if [ -d "out" ]; then
    echo "Cleaning previous output directory..."
    rm -rf out/switch
    rm -f out/switch_application.zip
    rm -f out/switch_sysmod.zip
fi

# Create directory structure
mkdir -p out/switch/config/ftpsrv
cp assets/config.ini.template out/switch/config/ftpsrv/

# Package application
mkdir -p out/switch/switch
cp build/switch/*.nro out/switch/switch/ftpsrv.nro
echo "Creating application zip..."
(cd out/switch && zip -r9 ../switch_application.zip switch config)

# Package sysmodule
mkdir -p out/switch/atmosphere/contents/
cp -r build/switch/420000000000011B out/switch/atmosphere/contents/
echo "Creating sysmodule zip..."
(cd out/switch && zip -r9 ../switch_sysmod.zip atmosphere config)

echo ""
echo "Build and packaging complete."
echo "  Application: out/switch_application.zip"
echo "  Sysmodule:   out/switch_sysmod.zip"