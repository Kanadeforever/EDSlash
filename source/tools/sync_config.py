"""为旧发布配置补入模板新增键，保留用户已有数值、绑定和注释。"""
from pathlib import Path
import os
import re
import tempfile
import tomllib


HEADER = re.compile(r'^\s*\[([A-Za-z0-9_.-]+)\]\s*(?:#.*)?$')
KEY = re.compile(r'^\s*([A-Za-z0-9_-]+)\s*=')


def table(document, name):
    # TOML解析结果是嵌套字典；沿点分段取到当前配置段，不把点名当成单个键。
    current = document
    for part in name.split('.'):
        if not isinstance(current, dict) or part not in current:
            return None
        current = current[part]
    return current


def leaves(document, prefix=()):
    # 扁平快照仅用于核对所有原有值都没变，包括模板以外的角色快捷绑定。
    result = {}
    for key, value in document.items():
        if isinstance(value, dict):
            result.update(leaves(value, prefix + (key,)))
        else:
            result[prefix + (key,)] = value
    return result


def merged_text(existing, template):
    """解析成功后再补键；不能解析或不能安全合并时抛错，调用方不得覆盖。"""
    original = tomllib.loads(existing.decode('utf-8'))
    defaults = tomllib.loads(template.decode('utf-8'))
    lines = existing.decode('utf-8').splitlines()
    template_lines = template.decode('utf-8').splitlines()
    additions = {}
    section = ''
    comments = []
    for line in template_lines:
        header = HEADER.match(line)
        if header:
            section = header.group(1)
            comments = []
            continue
        key = KEY.match(line)
        if key:
            old = table(original, section) if section else original
            if old is not None and not isinstance(old, dict):
                raise ValueError(f'配置段{section}不是表，不能安全补键')
            if old is None or key.group(1) not in old:
                additions.setdefault(section, []).extend(comments + [line])
            comments = []
        elif line.lstrip().startswith('#'):
            comments.append(line)
        else:
            comments = []
    if not additions:
        return existing, []

    # 在已存在段的末尾补入新增项；不能再次声明同一个TOML段，否则会变成重复定义。
    output = []
    current = ''
    for line in lines:
        header = HEADER.match(line)
        if header:
            output.extend(additions.pop(current, []))
            current = header.group(1)
        output.append(line)
    output.extend(additions.pop(current, []))
    for name, values in additions.items():
        if not name:
            raise ValueError('无法安全定位根配置键')
        if name == 'logging':
            # 总开关放第一个表之前，保留原根键和所有已有内容的所属表。
            at = next((i for i, line in enumerate(output) if HEADER.match(line)), len(output))
            output[at:at] = [f'[{name}]'] + values + ['']
        else:
            output.extend(['', f'[{name}]'] + values)
    merged = ('\r\n'.join(output).rstrip() + '\r\n').encode('utf-8')
    decoded = tomllib.loads(merged.decode('utf-8'))
    # 最后核对所有旧值及所有模板键，确保注入位置没有改变值所属的配置段。
    before, after, required = leaves(original), leaves(decoded), leaves(defaults)
    if any(key not in after or after[key] != value for key, value in before.items()):
        raise ValueError('合并会改变原有配置值，原文件未改')
    if any(key not in after for key in required):
        raise ValueError('合并后仍缺模板键，原文件未改')
    return merged, ['.'.join(key) for key in required if key not in before]


def plan(path, template):
    # 发布前先读取并验证所有目录的旧配置；没有文件才使用整个默认模板。
    path = Path(path)
    previous = path.read_bytes() if path.exists() else None
    if previous is None:
        tomllib.loads(template.decode('utf-8'))
        return path, previous, template, ['默认配置']
    merged, keys = merged_text(previous, template)
    return path, previous, merged, keys


def apply(planned):
    path, previous, merged, keys = planned
    if previous == merged:
        return []
    path.parent.mkdir(parents=True, exist_ok=True)
    # 构建过程中若配置被另一处修改，拒绝用旧快照覆盖；临时文件同目录后原子替换。
    current = path.read_bytes() if path.exists() else None
    if current != previous:
        raise ValueError(f'{path}已被外部修改，停止配置同步')
    descriptor, temporary = tempfile.mkstemp(prefix=path.name + '.', suffix='.tmp', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'wb') as stream:
            stream.write(merged)
        os.replace(temporary, path)
    finally:
        if Path(temporary).exists():
            Path(temporary).unlink()
    return keys
