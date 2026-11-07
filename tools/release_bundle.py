"""Package the hardware-tested BDA for CI; verify final files after download."""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TAG_PATTERN = r"v(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)(?:-(?:alpha|beta|rc)\.[1-9][0-9]*)?"
ASSETS = {
    "H1PVZPortable.bda", "H1PVZ-full-install.zip",
    "H1PVZ-full-install.zip.sha256", "H1PVZPortable.build.json",
    "native-verification.json", "RELEASE-NOTES.md", "release-manifest.json",
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def validate_tag(tag):
    require(not tag or re.fullmatch(TAG_PATTERN, tag), "Use vMAJOR.MINOR.PATCH (optionally -alpha.N, -beta.N or -rc.N)")
    return tag


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def read(path):
    return json.loads(path.read_text(encoding="utf-8"))


def verify_bundle(directory, commit, tag=""):
    validate_tag(tag)
    require(re.fullmatch(r"[0-9a-f]{40}", commit), "Expected a full source commit")
    require({p.name for p in directory.iterdir()} == ASSETS | {"SHA256SUMS.txt"}, "Unexpected or missing release files")
    expected = {}
    for line in (directory / "SHA256SUMS.txt").read_text(encoding="ascii").splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  ([A-Za-z0-9_.-]+)", line)
        require(match is not None, "Invalid checksum entry")
        digest, name = match.groups()
        require(name in ASSETS and name not in expected, "Unexpected or duplicate checksum filename")
        expected[name] = digest
    require(set(expected) == ASSETS, "Incomplete checksum list")
    for name, digest in expected.items():
        require(sha(directory / name) == digest, "SHA-256 mismatch: " + name)
    manifest = read(directory / "release-manifest.json")
    require(manifest["source_commit"] == commit and manifest["tag"] == tag, "Release source/tag mismatch")
    require(manifest["distribution"] == "hardware-tested-prebuilt", "Unexpected distribution type")
    receipt = read(directory / "H1PVZPortable.build.json")
    proof = read(directory / "native-verification.json")
    bda_sha = expected["H1PVZPortable.bda"]
    require(bda_sha == manifest["program_sha256"] == receipt["sha256"] == proof["program"]["bda_sha256"], "Program/proof mismatch")
    require(expected["H1PVZPortable.build.json"] == proof["program"]["build_receipt_sha256"], "Build receipt/proof mismatch")
    require(proof["passed"] is True and proof["program"]["passed"] is True, "Missing hardware verification")
    require((directory / "H1PVZ-full-install.zip.sha256").read_text(encoding="ascii") == expected["H1PVZ-full-install.zip"] + "  H1PVZ-full-install.zip\n", "ZIP checksum mismatch")
    with zipfile.ZipFile(directory / "H1PVZ-full-install.zip") as archive:
        package = json.loads(archive.read("package-manifest.json"))
        files = package["files"]
        require(package["includes_savedata"] is False, "Package must exclude personal savedata")
        require(set(archive.namelist()) == set(files) | {"package-manifest.json"} and len(archive.namelist()) == len(files) + 1, "ZIP member mismatch")
        for name, digest in files.items():
            require(not name.startswith("/") and ".." not in name.split("/") and "userdata" not in name.lower().split("/"), "Unsafe ZIP member: " + name)
            with archive.open(name) as stream:
                require(hashlib.file_digest(stream, "sha256").hexdigest() == digest, "ZIP member checksum: " + name)
        require(files["A/应用/程序/H1PVZPortable.bda"] == package["program_sha256"] == bda_sha, "ZIP/raw BDA mismatch")
        resources = {name: digest for name, digest in files.items() if name.startswith("B/应用/数据/游戏/PVZ/")}
        require(len(resources) == manifest["resource_files"], "ZIP resource count mismatch")
    print("Verified downloaded release files, source, hardware proof and complete install ZIP:", directory)


def build_bundle(directory, tag=""):
    validate_tag(tag)
    directory = directory.resolve()
    require(directory.is_relative_to(ROOT / "dist") and not directory.exists(), "Choose a new output directory inside dist; preserve existing bundles")
    commit = subprocess.check_output(["git", "-c", "safe.directory=" + ROOT.as_posix(), "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    require(re.fullmatch(r"[0-9a-f]{40}", commit), "Invalid source commit")
    directory.mkdir(parents=True)
    subprocess.run([sys.executable, str(ROOT / "tools/package_install.py"), "--output", str(directory / "H1PVZ-full-install.zip")], cwd=ROOT, check=True)
    for source, name in (
        ("prebuilt/H1PVZPortable.bda", "H1PVZPortable.bda"),
        ("prebuilt/H1PVZPortable.build.json", "H1PVZPortable.build.json"),
        ("docs/verification/final-native-verification.json", "native-verification.json"),
    ):
        shutil.copyfile(ROOT / source, directory / name)
    resources = read(ROOT / "provenance/resources.json")
    sdk = read(ROOT / "SDK.json")
    bda_sha = sha(directory / "H1PVZPortable.bda")
    manifest = {"source_commit": commit, "tag": tag, "sdk_commit": sdk["commit"],
                "distribution": "hardware-tested-prebuilt", "program_sha256": bda_sha,
                "resource_files": len(resources["files"]),
                "resource_inventory_sha256": sha(ROOT / "provenance/resources.json")}
    (directory / "release-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")
    notes = f"""# 植物僵尸 {tag or ('开发快照 ' + commit[:12])}

适用步步高 H1 / Y100 V1.41，480×272，触屏操作、中文界面。

下载 `H1PVZ-full-install.zip` 并解压：将 `A/` 中的程序复制到 H1 A 盘，将 `B/` 中的完整资源目录复制到 B 盘。更新前保留原来的 `B:/应用/数据/游戏/PVZ/userdata/`，安装包不含个人存档。A 盘存在有效 PVZ 资源目录时会优先使用 A 盘。

`H1PVZPortable.bda` 是同一程序的单独下载，不包含运行所需资源。使用 `SHA256SUMS.txt` 核验下载文件；包内附安装说明与许可声明。

CI 校验固定 SDK、源码输入、资源清单、BDA 格式及真机证据，打包仓库中已通过真机验证的程序，下载制品后再次核验每个文件和 ZIP 成员。本流程没有重新编译 C/C++，也没有重新执行真机测试。

当前真机记录：第六关 40 株植物约 12.07 FPS，主菜单退出约 1.80 秒；密集场景首次打开选项约 575 毫秒。尚未达到整体 20 FPS，部分动态状态仍未完全离线烘焙。具体范围见 `native-verification.json` 和仓库验证文档。

源码提交：`{commit}`
SDK 提交：`{sdk['commit']}`
程序 SHA-256：`{bda_sha}`
"""
    (directory / "RELEASE-NOTES.md").write_text(notes, encoding="utf-8", newline="\n")
    (directory / "SHA256SUMS.txt").write_text("".join(sha(directory / name) + "  " + name + "\n" for name in sorted(ASSETS)), encoding="ascii", newline="\n")
    verify_bundle(directory, commit, tag)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("build", "verify"))
    parser.add_argument("--directory", type=Path, default=ROOT / "dist/release")
    parser.add_argument("--commit", help="Expected source commit when verifying a download")
    parser.add_argument("--tag", default="")
    args = parser.parse_args()
    if args.mode == "build":
        build_bundle(args.directory, args.tag)
    else:
        require(args.commit is not None, "verify requires --commit")
        verify_bundle(args.directory, args.commit, args.tag)


if __name__ == "__main__":
    main()
