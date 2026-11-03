"""Replace only reviewed image-profile files in this project's idle owned NAND.

All FAT patches are accumulated against one original logical image. Each changed
256-KiB FTL unit is ECC-encoded once. Existing save bytes are compared afterwards.
The full baseline resource tree remains untouched; replaced file bytes are
backed up inside the owned work directory before any NAND write.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import sys
from datetime import datetime, timezone
import verify_firmware as vf


def hash_bytes(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--resources', type=Path, default=vf.ROOT / 'private/resources-h1')
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    work, owner = vf.owned_work(args.work)
    source = args.resources.resolve()
    if not source.is_relative_to((vf.ROOT / 'private').resolve()):
        raise ValueError('Select only this project private resource profile')
    vf.load_reference().ensure_template_idle(work / 'h1-system.raw')
    manifest_path = source / 'h1-scale-manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf8'))
    if manifest['schema'] != 'h1-pvz-offline-scale-1':
        raise ValueError('Unexpected offline-profile manifest')
    names = sorted(set(manifest['scaled_images']) | {'image-metadata.h1x', 'resource-index.json'})
    payloads = {name: (source / name).read_bytes() for name in names}
    for name, item in manifest['scaled_images'].items():
        if hash_bytes(payloads[name]) != item['sha256']:
            raise ValueError('Profile file changed: ' + name)
    if hash_bytes(payloads['image-metadata.h1x']) != manifest['metadata_index']['sha256']:
        raise ValueError('Profile index changed')
    adapter, sdk, installer, provenance = vf.load_volume(work)
    changed = {}
    reserved = set()
    before = {}
    report = dict(schema='h1-private-resource-profile-deployment-1', owner_id=owner['id'],
        resource_drive='A', physical_device_used=False, game_RAM_written=False,
        profile_source=str(source), profile_manifest_sha256=vf.digest(manifest_path),
        dry_run=args.dry_run, files=[], save_bytes_preserved=False)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S-%f')
    backup = work / ('resource-backup-' + stamp)
    if not args.dry_run:
        backup.mkdir()
    with sdk.H1Fat16(work / 'h1-system.raw') as volume:
        entries = vf.volume_entries(volume)
        files = {e.path.casefold(): e for e in entries if not e.is_directory}
        directories = {e.path.casefold(): e for e in entries if e.is_directory}
        for entry in entries:
            if not entry.is_directory and entry.path.casefold().startswith('/pvzsave/'):
                before[entry.path] = hash_bytes(volume.read_file(entry))
        for name in names:
            target = files.get(('/PVZ/' + name).casefold())
            if target is None:
                raise FileNotFoundError('Profile may only replace existing resource files: ' + name)
            original = volume.read_file(target)
            if not args.dry_run:
                destination = backup / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(original)
            # The directory parser concatenates chains for LFNs. Translate its
            # relative offset back through the actual chain before editing.
            parent_name = target.path.rsplit('/', 1)[0].casefold()
            parent = directories[parent_name]
            dir_chain = volume.cluster_chain(parent.first_cluster)
            relative = target.directory_entry_offset - volume.cluster_offset(parent.first_cluster)
            directory_offset = volume.cluster_offset(dir_chain[relative // volume.geometry.cluster_size]) + relative % volume.geometry.cluster_size
            raw_entry = volume.reader.read(directory_offset, 32)
            if int.from_bytes(raw_entry[26:28], 'little') != target.first_cluster or int.from_bytes(raw_entry[28:32], 'little') != len(original):
                raise ValueError('FAT entry location did not verify: ' + target.path)
            payload = payloads[name]
            chain = volume.cluster_chain(target.first_cluster)
            required = sdk.required_cluster_count(len(payload), volume.geometry.cluster_size)
            extra = []
            if required > len(chain):
                for cluster in range(2, volume.geometry.max_data_cluster + 1):
                    if cluster not in reserved and all(volume.fat_entry(cluster, i) == 0 for i in range(volume.geometry.fat_copies)):
                        extra.append(cluster)
                        reserved.add(cluster)
                        if len(extra) == required - len(chain):
                            break
                if len(extra) != required - len(chain):
                    raise OSError('Not enough mutually free FAT clusters')
                if chain:
                    sdk.patch_fat_entry(changed, volume, chain[-1], extra[0])
                else:
                    sdk.patch_unit(changed, volume, directory_offset + 26, extra[0].to_bytes(2, 'little'))
                for i, cluster in enumerate(extra):
                    sdk.patch_fat_entry(changed, volume, cluster, extra[i + 1] if i + 1 < len(extra) else 0xffff)
                chain += extra
            capacity = len(chain) * volume.geometry.cluster_size
            padded = payload + bytes(capacity - len(payload))
            for i, cluster in enumerate(chain):
                sdk.patch_unit(changed, volume, volume.cluster_offset(cluster),
                               padded[i * volume.geometry.cluster_size:(i + 1) * volume.geometry.cluster_size])
            sdk.patch_unit(changed, volume, directory_offset + 28, len(payload).to_bytes(4, 'little'))
            report['files'].append(dict(path=target.path, before_bytes=len(original), before_sha256=hash_bytes(original),
                bytes=len(payload), sha256=hash_bytes(payload), clusters_added=extra))
        records, newly_allocated = sdk.allocate_ftl_records(volume.scan, set(changed))
    report['changed_logical_units'] = sorted(changed)
    report['ECC_encoded_once_per_changed_unit'] = True
    report['backup_original_files'] = str(backup) if not args.dry_run else None
    if args.dry_run:
        print(json.dumps(report, ensure_ascii=False, indent=2));return
    # The file payload backup and all planning have completed before NAND writes.
    with (work / 'h1-system.raw').open('r+b', buffering=0) as output, adapter.EccEncoder() as encoder:
        for logical in sorted(changed):
            record = records[logical]
            adapter.write_mapped_unit(output, record, logical, bytes(changed[logical]),
                1 if logical in newly_allocated else ((record.sequence or 0) + 1) & 0xffff, encoder)
        output.flush();os.fsync(output.fileno())
    with sdk.H1Fat16(work / 'h1-system.raw') as volume:
        entries = vf.volume_entries(volume)
        files = {e.path.casefold(): e for e in entries if not e.is_directory}
        for item in report['files']:
            actual = volume.read_file(files[item['path'].casefold()])
            if len(actual) != item['bytes'] or hash_bytes(actual) != item['sha256']:
                raise ValueError('Profile read-back failed: ' + item['path'])
        after = {e.path: hash_bytes(volume.read_file(e)) for e in entries
                 if not e.is_directory and e.path.casefold().startswith('/pvzsave/')}
        if before != after:
            raise ValueError('Original save bytes changed during resource-only deployment')
        bad = [r for r in volume.scan.records if r.kind in ('bad', 'invalid', 'torn')]
        if bad:
            raise ValueError('FTL validation failed after deployment')
    report.update(save_bytes_preserved=True, preserved_save_files=before,
        selected_files_readback_verified=True, invalid_ftl_records=0,
        host_only_profile_files_not_deployed=['h1-scale-manifest.json', 'h1-scale-owner.json'])
    destination = work / ('resource-profile-' + stamp + '.json')
    vf.write_json(destination, report)
    vf.write_json(work / 'active-resource-profile.json', dict(profile_source=str(source),
        manifest_sha256=report['profile_manifest_sha256'], deployment_report=str(destination),
        resource_drive='A', profile=manifest['profile'], image_metadata_index_sha256=manifest['metadata_index']['sha256']))
    print(json.dumps(dict(report=str(destination), changed_files=len(names), changed_units=len(changed),
        selected_files_readback_verified=True, save_bytes_preserved=True), indent=2))


if __name__ == '__main__':
    main()
