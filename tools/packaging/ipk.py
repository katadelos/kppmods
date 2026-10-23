#!/usr/bin/env python3
"""Build core or a staged extension with eink-sdk's opkg packager.

Run in the Linux cross-build environment. OPKG_* ELF policy/tool variables are
passed through to build-opkg, exactly as in the Buildroot package pipeline.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path


def copy_payload(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    if source.is_dir():
        shutil.copytree(
            source,
            destination,
            symlinks=True,
            ignore=shutil.ignore_patterns("__pycache__", "packaging"),
        )
    else:
        shutil.copy2(source, destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kind", choices=["core", "extension"])
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--sdk", required=True, type=Path)
    parser.add_argument(
        "--depends", action="append", default=[], help="Additional opkg dependency"
    )
    parser.add_argument("--version", required=True, help="Core package version")
    args = parser.parse_args()
    source = args.source.resolve()
    env = dict(os.environ)
    if env.get("OPKG_ARCH") not in ("kindlehf", "kindlepw2"):
        parser.error("Set OPKG_ARCH to kindlehf or kindlepw2 for the target device")
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="kppmods-ipk-") as tmp:
        work = Path(tmp)
        target = work / "target"
        target.mkdir()

        hooks = {}
        start = "/var/run/eink-sdk/usr/bin/kppmods-start"
        if args.kind == "core":
            name, version = "kppmods", args.version
            copy_payload(source / "usr/bin", target / "usr/bin")
            for abi in ("abi0", "abi1"):
                copy_payload(
                    source / "usr/lib/kppmods" / abi, target / "usr/lib/kppmods" / abi
                )
            copy_payload(
                source / "usr/lib/kppmods/libkppmods-framework-exec.so",
                target / "usr/lib/kppmods/libkppmods-framework-exec.so",
            )
            copy_payload(
                source / "usr/share/licenses/kppmods",
                target / "usr/share/licenses/kppmods",
            )
            for resource in ("upstart/kppmods.conf", "upstart/kppmods-storage.conf",
                             "kppmods.sh", "kppmods-mntus"):
                copy_payload(source / "usr/share/kppmods" / resource,
                             target / "usr/share/kppmods" / resource)
            hooks["postinst"] = (
                "mkdir -p /mnt/us/documents\n"
                "cp /mnt/us/.eink-sdk/usr/share/kppmods/kppmods.sh "
                "/mnt/us/documents/kppmods.sh\n" + start
            )
            hooks["prerm"] = "initctl stop kppmods-storage || :\ninitctl stop kppmods || :"
            hooks["postrm"] = (
                '[ "${1:-}" != upgrade ] || exit 0\n'
                "if [ -f /mnt/us/documents/kppmods.sh ] && "
                "grep -q '^# kppmods package launcher$' /mnt/us/documents/kppmods.sh; then\n"
                "    rm /mnt/us/documents/kppmods.sh\n"
                "fi\n"
                "while grep -q '^# kppmods userstore handover$' /usr/sbin/mntus; do\n"
                "    umount /usr/sbin/mntus || exit 1\n"
                "done\n"
                "rm -f /var/run/kppmods-upstart/kppmods.conf /var/run/kppmods-upstart/kppmods-storage.conf\n"
                "rm -f /var/run/kppmods/userstore /var/run/kppmods/mntus.stock\n"
                "initctl reload-configuration"
            )
            depends = []
        else:
            manifest = json.loads((source / "manifest.json").read_text())
            extension = manifest["id"]
            if len(extension) >= 128 or not re.fullmatch(
                r"[a-z0-9][a-z0-9.-]*", extension
            ):
                parser.error("Extension ID must also be a valid opkg name")
            name, version = "kppmods-" + extension, manifest["version"]
            copy_payload(source, target / "usr/lib/kppmods/extensions" / extension)
            depends = ["kppmods"] + ["kppmods-" + x for x in manifest.get("needs", [])]
            packaging = source / "packaging"
            for service in sorted(packaging.glob("*.conf")):
                copy_payload(
                    service, target / "usr/share/kppmods/upstart" / service.name
                )
            for hook_name in ("postinst", "prerm", "postrm"):
                custom = packaging / hook_name
                body = (
                    custom.read_text()
                    if custom.is_file()
                    else ('"$reload" "$id"' if hook_name != "prerm" else "")
                )
                if not body:
                    continue
                environment = (
                    f"id={extension}\nreload={start}\n"
                    f"package=/var/run/eink-sdk/usr/lib/kppmods/extensions/{extension}\n"
                )
                # The new package's postinst owns upgrade activation.
                guard = (
                    '[ "${1:-}" != upgrade ] || exit 0\n'
                    if hook_name == "postrm"
                    else ""
                )
                hooks[hook_name] = guard + environment + body
        for hook_name, hook_body in hooks.items():
            hook_path = work / hook_name
            hook_path.write_text("#!/bin/sh\nset -eu\n" + hook_body + "\n")
            env["OPKG_" + hook_name.upper()] = str(hook_path)
        depends.extend(args.depends)
        env.update(
            OPKG_BUILDROOT_NAME=name,
            OPKG_NAME=name,
            OPKG_VERSION=version,
            OPKG_DEPENDS=" ".join(depends),
            OPKG_DESCRIPTION="KPP Mods " + name,
        )
        manifest_path = work / "manifest"
        manifest_path.write_text(
            "".join(
                name + ",./" + str(x.relative_to(target)) + "\n"
                for x in sorted(target.rglob("*"))
            )
        )
        subprocess.run(
            [
                str(args.sdk.resolve() / "support/scripts/build-opkg"),
                str(manifest_path),
                str(target),
                str(work / "pack"),
                str(args.output.resolve()),
                str(target),
            ],
            env=env,
            check=True,
        )


if __name__ == "__main__":
    main()
