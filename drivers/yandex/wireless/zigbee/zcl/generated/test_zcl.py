import difflib
import pathlib
import yatest.common as yc


def test_zap():
    zap = yc.work_path("zap-2022.1.10.AppImage")
    out_dir = pathlib.Path(yc.output_path("generated"))
    src_dir = pathlib.Path(yc.source_path("smart_devices/libs/zigbee/zcl/generated"))

    yc.execute([
        zap, "generate",
        "-z", yc.source_path("smart_devices/libs/zigbee/zcl/xml/library.xml"),
        "-g", yc.source_path("smart_devices/libs/zigbee/zcl/zap/zap.json"),
        "-o", out_dir
    ])

    update = yc.get_param("update")

    diff = []

    for out in out_dir.glob("*"):
        src = src_dir / out.name

        if update:
            src.write_text(out.read_text())
        else:
            diff.extend(difflib.unified_diff(
                a=out.read_text().splitlines(),
                b=src.read_text().splitlines(),
                fromfile=f'a/{out.name}',
                tofile=f'b/{src.name}',
                lineterm='',
            ))

    assert len(diff) == 0, "\n".join(diff)
