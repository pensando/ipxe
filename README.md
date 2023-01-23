# pensando/ipxe

## Overview

This repository contains the AMD Pensando ipxe changes that add
support for the ionic device.

## Building

Build ipxe with all the supported drivers including ionic.
```
cd src
make bin-x86_64-efi-sb/ipxe.efi
```

Build ipxe with support for the ionic device only.
```
cd src
make bin-x86_64-efi-sb/ionic.efi
```

Build ipxe with support for the ionic device in option rom format.
This image is included with the ionic device as the option rom used
for network booting using the device in the pre-execution environment (pxe).
```
cd src
make CONFIG=pen_ionic_efirom bin-x86_64-efi-sb/ionic.efirom
```
