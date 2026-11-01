# Zigbee Cluster Library implementation

## ZAP templates

See [ZAP template tutorial](https://github.com/project-chip/zap/blob/master/docs/template-tutorial.md).

## Code generation

After changing files in `xml/` or `zap/` directories, run (Linux only)
```sh
$ ya make smart_devices/libs/zigbee/zcl/generated -t --test-tag ya:manual --test-param update=1
```

Do not edit C++ code in `generated/` directory manually.
