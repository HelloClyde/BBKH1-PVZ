"""Copy existing private NAND resources/userdata to the requested Chinese root.

Only allocates a new subtree and appends its parent entry. Original /PVZ and
/PVZSAVE files and directory bytes are compared before/after; no save state is
generated or edited. The emulator must be stopped and its ownership must match.
"""
from pathlib import Path
from datetime import datetime, timezone
import argparse
import hashlib
import json
import os
import sys
import verify_firmware as vf

TARGET = '/应用/数据/游戏/PVZ'
PARENT = '/应用/数据/游戏'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def old_inventory(volume, entries):
    files = {}; directories = {}
    for entry in entries:
        lower = entry.path.casefold()
        if not any(lower == prefix or lower.startswith(prefix + '/') for prefix in ('/pvz', '/pvzsave')):
            continue
        if entry.is_directory:
            directories[entry.path] = sha(volume.read_chain(entry.first_cluster))
        else:
            files[entry.path] = dict(bytes=entry.size, sha256=sha(volume.read_file(entry)),
                                    first_cluster=entry.first_cluster)
    return dict(files=files, directories=directories)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    work, owner = vf.owned_work(args.work)
    vf.load_reference().ensure_template_idle(work / 'h1-system.raw')
    adapter, sdk, installer, provenance = vf.load_volume(work)
    sys.path.insert(0, str(vf.WORKSPACE / '.tools/h1-emulator'))
    from tools import make_fat16_image as fat
    stamp = datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S-%f')
    stage = work / ('chinese-root-stage-' + stamp) / 'PVZ'
    stage.mkdir(parents=True)
    expected = {}; copied_from = {}; changes = {}
    with sdk.H1Fat16(work / 'h1-system.raw') as volume:
        entries = vf.volume_entries(volume)
        if any(e.path.casefold() == TARGET.casefold() for e in entries):
            raise FileExistsError('Target already exists; refuse to overwrite any existing subtree')
        parents = [e for e in entries if e.is_directory and e.path == PARENT]
        if len(parents) != 1:
            raise ValueError('Expected existing Unicode game-data parent directory')
        parent = parents[0]
        before = old_inventory(volume, entries)
        for entry in entries:
            if entry.is_directory:
                continue
            if entry.path.casefold().startswith('/pvz/'):
                relative = entry.path[len('/PVZ/'):]
            elif entry.path.casefold().startswith('/pvzsave/userdata/'):
                relative = entry.path[len('/PVZSAVE/'):]
            else:
                continue
            if relative.casefold() in {name.casefold() for name in expected}:
                raise ValueError('Resource/userdata paths collide: ' + relative)
            payload = volume.read_file(entry)
            destination = stage / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(payload)
            expected[relative] = dict(bytes=len(payload), sha256=sha(payload))
            copied_from[relative] = entry.path
        node = fat.read_node(stage, 'PVZ')
        synthetic = fat.FsNode(name='', source=None, is_dir=True, children=[node])
        fat.assign_short_names(synthetic)
        parent_data = volume.read_chain(parent.first_cluster)
        used = {parent_data[i:i + 11] for i in range(0, len(parent_data), 32)
                if parent_data[i] not in (0, 0xe5) and parent_data[i + 11] != 0x0f}
        node.short_name = fat.make_short_name('PVZ', used, True, 1)
        nodes = [node] + fat.iter_nodes(node)
        counts = {}
        for item in nodes:
            if item.is_dir:
                item.first_cluster = 2  # Include dot/dotdot when estimating.
                size = len(fat.directory_entries(item, 0))
                counts[id(item)] = max(1, (size + volume.geometry.cluster_size - 1) // volume.geometry.cluster_size)
            else:
                counts[id(item)] = (item.size + volume.geometry.cluster_size - 1) // volume.geometry.cluster_size
        allocated = sdk.select_free_clusters(volume, sum(counts.values()))
        chains = {}; position = 0
        for item in nodes:
            count = counts[id(item)];chain = allocated[position:position + count];position += count
            item.first_cluster = chain[0] if chain else 0
            chains[id(item)] = chain
            for i, cluster in enumerate(chain):
                sdk.patch_fat_entry(changes, volume, cluster, chain[i + 1] if i + 1 < len(chain) else 0xffff)
        def parents_for(item, parent_cluster):
            item.parent_cluster = parent_cluster
            for child in item.children:
                parents_for(child, item.first_cluster)
        parents_for(node, parent.first_cluster)
        for item in nodes:
            payload = fat.directory_entries(item, item.parent_cluster) if item.is_dir else item.source.read_bytes()
            chain = chains[id(item)]
            payload += bytes(len(chain) * volume.geometry.cluster_size - len(payload))
            for i, cluster in enumerate(chain):
                sdk.patch_unit(changes, volume, volume.cluster_offset(cluster),
                    payload[i * volume.geometry.cluster_size:(i + 1) * volume.geometry.cluster_size])
        lfn = b''.join(fat.make_lfn_entries('PVZ', fat.lfn_checksum(node.short_name)))
        entry_data = lfn + fat.make_short_entry(node)
        slot = installer.free_directory_slots(volume, parent, len(entry_data) // 32)
        sdk.patch_unit(changes, volume, slot, entry_data)
        records, new_records = sdk.allocate_ftl_records(volume.scan, set(changes))
    report = dict(schema='h1-private-chinese-root-copy-1', owner_id=owner['id'], target_unicode=TARGET,
        native_prefix_gbk_hex=('A:' + TARGET.replace('/', '\\')).encode('gbk').hex(),
        unicode_lfn_encoding='UTF-16LE', old_roots_before=before, expected_new_files=expected,
        copied_from=copied_from, copied_userdata_without_editing=True,
        optional_old_log_and_cache_copied=False, allocated_clusters=len(allocated),
        changed_ftl_units=len(changes), ECC_once_per_changed_unit=True,
        game_RAM_written=False, physical_device_used=False, stage=str(stage))
    vf.write_json(work / ('chinese-root-plan-' + stamp + '.json'), report)
    with (work / 'h1-system.raw').open('r+b', buffering=0) as output, adapter.EccEncoder() as encoder:
        for logical in sorted(changes):
            record = records[logical]
            adapter.write_mapped_unit(output, record, logical, bytes(changes[logical]),
                1 if logical in new_records else ((record.sequence or 0) + 1) & 0xffff, encoder)
        output.flush();os.fsync(output.fileno())
    with sdk.H1Fat16(work / 'h1-system.raw') as volume:
        entries = vf.volume_entries(volume)
        files = {e.path.casefold(): e for e in entries if not e.is_directory}
        for relative, item in expected.items():
            actual = volume.read_file(files[(TARGET + '/' + relative).casefold()])
            if len(actual) != item['bytes'] or sha(actual) != item['sha256']:
                raise ValueError('New Unicode-root file readback mismatch: ' + relative)
        if old_inventory(volume, entries) != before:
            raise ValueError('Old resource/save file bytes or directory metadata changed')
        if any(r.kind in ('bad', 'invalid', 'torn') for r in volume.scan.records):
            raise ValueError('FTL validation failed')
    report.update(all_new_files_readback_verified=True, old_resources_and_saves_unchanged=True,
                  invalid_ftl_records=0, result='copied-awaiting-new-path-native-BDA')
    destination = work / 'chinese-root-copy-verification.json'
    vf.write_json(destination, report)
    print(json.dumps(dict(report=str(destination), files=len(expected), bytes=sum(x['bytes'] for x in expected.values()),
        old_roots_unchanged=True, target=TARGET, native_prefix_gbk_hex=report['native_prefix_gbk_hex']), ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
