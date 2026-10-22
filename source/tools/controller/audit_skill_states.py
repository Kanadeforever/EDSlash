"""独立审计两作的职业归属、技能选择码和可重叠状态特征；只写调查派生物。"""
from pathlib import Path
import collections
import csv
import json
import argparse
import hashlib
import struct
import sys

BASE = None  # main指定输出目录，导入本模块不会读写游戏文件。
ROLES = {0: "剑客", 1: "女侠", 2: "力士", 3: "长老", 4: "老英雄"}


def audit(game):
    root = BASE / "原始派生" / game
    rows = list(csv.DictReader((root / "player/skill.txt").open(encoding="utf-8"), delimiter="\t"))
    initial = list(csv.DictReader((root / "player/Init.txt").open(encoding="utf-8"), delimiter="\t"))
    basic = {}
    # 初始左右手给出的全部选择码是职业基础动作归属的独立佐证。
    for index, row in enumerate(initial):
        for field in ["初始左手技能", "初始右手技能"]:
            for selector in row[field].split("*"):
                basic[int(selector)] = ROLES[index]
    methods = collections.defaultdict(list)
    for line_no, line in enumerate((root / "Skill.ini").read_text(encoding="utf-8").splitlines(), 1):
        t = line.split("\t")
        if len(t) >= 38 and t[3].isdigit():
            assert len(t) == 38 + 8 * int(t[37]), (game, line_no)
            methods[int(t[3])].append({"源行": line_no, "字段": t, "子命令": [list(map(int, t[i:i+8])) for i in range(38, len(t), 8)]})
    effects = collections.defaultdict(list)
    for line_no, line in enumerate((root / "SkillChild.ini").read_text(encoding="utf-8").splitlines(), 1):
        t = line.split("\t")
        if len(t) >= 16 and t[3].isdigit():
            assert len(t) == 16 + 8 * int(t[15]), (game, line_no)
            effects[int(t[3])].append({"源行": line_no, "字段": t, "子命令": [list(map(int, t[i:i+8])) for i in range(16, len(t), 8)]})
    # 图节点带M/E前缀，避免同编号Method和Effect被误合并。
    records = {**{f"M{k}": v for k, v in methods.items()}, **{f"E{k}": v for k, v in effects.items()}}
    graphs = [{}, {}]
    for index, graph in enumerate(graphs):
        for node, candidates in records.items():
            graph[node] = [{"源": node, "源行": record["源行"], "目标": ("M" if c[2] == 9 else "E") + str(c[3]),
                            "事件类型": c[0], "事件参数": c[1], "操作码": c[2], "完整命令": c}
                           for record in (candidates[:1] if index == 0 else candidates)
                           for c in record["子命令"] if c[2] in (2, 9)]

    def reachable(direct, graph):
        visited, todo, edges = set(), [f"M{mid}" for mid in direct], []
        while todo:
            node = todo.pop()
            if node in visited:
                continue
            visited.add(node)
            edges.extend(graph.get(node, []))
            todo.extend(edge["目标"] for edge in graph.get(node, []))
        # DFS回边提供实际闭环证据，保留节点路径；不把共享后继误报成循环。
        color, path, cycles = {}, [], []
        def walk(node):
            color[node] = 1
            path.append(node)
            for edge in graph.get(node, []):
                target = edge["目标"]
                if color.get(target) == 1:
                    cycle = path[path.index(target):] + [target]
                    if cycle not in cycles:
                        cycles.append(cycle)
                elif color.get(target, 0) == 0:
                    walk(target)
            path.pop()
            color[node] = 2
        for node in sorted(visited):
            if color.get(node, 0) == 0:
                walk(node)
        return {"Method": sorted(int(n[1:]) for n in visited if n.startswith("M") and n in records),
                "Effect": sorted(int(n[1:]) for n in visited if n.startswith("E") and n in records),
                "缺失或动态引用": [e for e in edges if e["目标"] not in records], "边": edges, "循环回边路径": cycles,
                "重复候选节点": {n: [r["源行"] for r in records[n]] for n in visited if n in records and len(records[n]) > 1}}
    groups = {}
    for line_no, line in enumerate((root / "Skill.gop").read_text(encoding="utf-8").splitlines(), 1):
        t = line.split("\t")
        if len(t) >= 8 and t[3].isdigit():
            assert len(t) == 7 + int(t[6]), (game, line_no)
            assert int(t[3]) not in groups, (game, line_no)
            groups[int(t[3])] = {"源行": line_no, "字段": t, "Methods": list(map(int, t[7:]))}
    byselector = collections.defaultdict(list)
    for line_no, row in enumerate(rows, 2):
        family, selector = int(row["系号"]), int(row["招式ID"])
        role = ROLES[(family-1)//4] if family else basic.get(selector)
        assert role, (game, line_no, selector)
        row = dict(row)
        row.update({"源行": line_no, "职业": role})
        byselector[selector].append(row)
    result = []
    for selector, skill_rows in byselector.items():
        group = groups[selector]
        direct = group["Methods"]
        reachable_graph = reachable(direct, graphs[0])
        candidate_graph = reachable(direct, graphs[1])
        closure = set(reachable_graph["Method"])
        missing = [int(e["目标"][1:]) for e in reachable_graph["缺失或动态引用"] if e["目标"].startswith("M")]
        duplicates = [mid for mid in closure if len(methods[mid]) > 1]
        features, details = set(), []
        for mid in sorted(closure):
            for candidate_index, method in enumerate(methods[mid]):
                t, commands = method["字段"], method["子命令"]
                window = list(map(int, t[6].split("*")))
                if mid in direct and candidate_index == 0:
                    if window[0] >= 1000:
                        features.add("起手计时窗口续接")
                    elif window[0] > 0 and direct.index(mid) > 0:
                        features.add("收招计时窗口续接")
                if int(t[15]) == 5 and candidate_index == 0:
                    features.add("执行子型5")
                if any(c[2] == 9 for c in commands) and candidate_index == 0:
                    features.add("自动派生命令9")
                if int(t[20]) == 1 and candidate_index == 0:
                    features.add("可变落点标志")
                if any(c[0] == 4 for c in commands) and candidate_index == 0:
                    features.add("含命令4_条件性持有标志候选")
                details.append({"Method": mid, "源行": method["源行"], "名称": t[1], "直接成员": mid in direct,
                                "首条有效候选": candidate_index == 0,
                                "窗口": t[6], "固定列3": t[2], "节点资格列5": t[4], "执行子型列16": t[15],
                                "落点列21": t[20], "命令类型": dict(collections.Counter(c[2] for c in commands)),
                                "自动派生": [c for c in commands if c[2] == 9], "命令4": [c for c in commands if c[0] == 4]})
        if len(direct) > 1:
            features.add("有序多Method组")
        specials = [{k: r[k] for k in ["等级", "特别数据1", "特别数据2", "特别数据3"]} for r in skill_rows
                    if any(r[k] for k in ["特别数据1", "特别数据2", "特别数据3"])]
        if specials:
            features.add("有技能表特殊数据_语义待核")
        if not features:
            features.add("未见已枚举特殊特征")
        result.append({"职业": skill_rows[0]["职业"], "选择码": selector,
                       "技能名称": list(dict.fromkeys(r["名称"] for r in skill_rows)),
                       "等级行": [{k: r[k] for k in ["源行", "ID", "等级", "系号", "组号"]} for r in skill_rows],
                       "技能表特殊数据": specials, "特征": sorted(features), "GOP源行": group["源行"],
                       "GOP固定列": group["字段"][:7], "直接Methods": direct, "自动派生闭包": sorted(closure),
                       "缺Method": sorted(set(missing)), "重复Method": sorted(set(duplicates)), "Method明细": details,
                       "跨Method与Effect可达图": reachable_graph, "保留全部重复候选图": candidate_graph})
    counts = {}
    for role in ROLES.values():
        selected = [r for r in result if r["职业"] == role]
        if not selected:
            continue
        counts[role] = {"数据行": sum(len(r["等级行"]) for r in selected), "选择码": len(selected),
                        "技能名数": len({n for r in selected for n in r["技能名称"]}),
                        "技能组数": len({g["组号"] for r in selected for g in r["等级行"] if g["组号"] != "-1"}),
                        "基础动作数": len([r for r in selected if all(g["组号"] == "-1" for g in r["等级行"])]),
                        "直接Method数": len({m for r in selected for m in r["直接Methods"]}),
                        "闭包Method数": len({m for r in selected for m in r["自动派生闭包"]}),
                        "闭包Effect数": len({e for r in selected for e in r["跨Method与Effect可达图"]["Effect"]}),
                        "有循环的选择码数": sum(bool(r["跨Method与Effect可达图"]["循环回边路径"]) for r in selected),
                        "有缺失或动态引用的选择码数": sum(bool(r["跨Method与Effect可达图"]["缺失或动态引用"]) for r in selected),
                        "特征计数": dict(collections.Counter(f for r in selected for f in r["特征"]))}
    return {"作品": game, "有效记录策略": "编号重复时首条生效；所有原始候选另存。", "汇总": counts,
            "重复技能ID": {k: v for k, v in collections.Counter(r["ID"] for r in rows).items() if v > 1},
            "重复Method全局": {str(k): [m["源行"] for m in v] for k, v in methods.items() if len(v) > 1},
            "选择码清单": result}



def extract_resources(source, edition):
    """原包完整读取两次核对稳定性；只写输出目录中的UTF-8文本派生物。"""
    out=BASE/"原始派生"/edition
    before=source.stat();data=source.read_bytes();after=source.stat()
    assert (before.st_size,before.st_mtime_ns)==(after.st_size,after.st_mtime_ns),"源文件复制中，暂不采信"
    count=struct.unpack_from("<I",data)[0]&0x7fffffff;keys=len(data)-(count+0x61)*4
    assert 0<count<10000 and 4+count*136<=keys
    selected={"Skill.gop","Skill.ini","SkillChild.ini","player\\skill.txt","player\\Init.txt","ActMdl.txt","RoleEffectMdl.txt"}
    entries=[];exported=[]
    for idx in range(count):
     key=struct.unpack_from("<I",data,keys+idx*4)[0]
     vals=struct.unpack_from("<34I",data,4+idx*136)
     record=struct.pack("<34I",*[(v-key-9*i**3)&0xffffffff for i,v in enumerate(vals)])
     name=record[:128].split(b"\0",1)[0].decode("gb18030");offset,size=struct.unpack_from("<II",record,128)
     assert 4+count*136<=offset<=offset+size<=keys
     item={"路径":name,"起点":offset,"长度":size}
     if name in selected:
      payload=data[offset:offset+size];n=len(payload)//4*4
      words=[(v-key-99*i*i)&0xffffffff for i,(v,) in enumerate(struct.iter_unpack("<I",payload[:n]))]
      plain=struct.pack(f"<{len(words)}I",*words)+payload[n:]
      text=plain.decode("gb18030");target=out/name.replace("\\","/")
      assert target.resolve().is_relative_to(out.resolve())
      target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(text.replace("\r\n","\n").replace("\n","\r\n").encode("utf-8"))
      item["原始正文SHA256"]=hashlib.sha256(plain).hexdigest();exported.append(name)
     entries.append(item)
    assert selected==set(exported),(selected-set(exported))
    assert hashlib.sha256(source.read_bytes()).digest()==hashlib.sha256(data).digest(),"源文件在读取期间发生变化"
    manifest={"输入":str(source),"SHA256":hashlib.sha256(data).hexdigest(),"目录条数":count,"条目":entries}
    (out/"资源来源与校验.json").write_bytes((json.dumps(manifest,ensure_ascii=False,indent=2)+"\n").replace("\n","\r\n").encode())
    return manifest

def main():
    global BASE
    sys.stdout.reconfigure(encoding="utf-8")
    parser=argparse.ArgumentParser(description="只读整理本体与外传全部职业的技能阶段和效果引用。")
    parser.add_argument("--base-fol",type=Path,required=True,help="本体mb.fol，只读")
    parser.add_argument("--expansion-fol",type=Path,required=True,help="外传mb.fol，只读")
    parser.add_argument("--output",type=Path,default=Path(__file__).resolve().parents[3]/".build/skill_state_audit",help="文本输出目录，不能是游戏输入目录")
    args=parser.parse_args();BASE=args.output.resolve()
    sources=[("本体",args.base_fol.resolve()),("外传",args.expansion_fol.resolve())]
    project=Path(__file__).resolve().parents[3]
    for protected in [project/"参考资料",project/"archive",*[p.parent for _,p in sources]]:
        if BASE.is_relative_to(protected.resolve()):parser.error("输出不能写入参考资料、archive或原游戏目录")
    provenance={edition:extract_resources(path,edition) for edition,path in sources}
    report={"边界":"原生已核对样本使用首条重复编号；资源特征可重叠，不代替实机验收。只覆盖显式code2/9引用。",
            "资源来源":provenance,"作品":[audit("本体"),audit("外传")]}
    (BASE/"双作品职业技能状态审计.json").write_bytes((json.dumps(report,ensure_ascii=False,indent=2)+"\n").replace("\n","\r\n").encode("utf-8"))
    for game in report["作品"]:
        for role,stats in game["汇总"].items():
            print(game["作品"],role,"等级记录",stats["数据行"],"选择码",stats["选择码"],"动作",stats["闭包Method数"],"效果",stats["闭包Effect数"])
    print("完整文本清单：",BASE/"双作品职业技能状态审计.json")

if __name__=="__main__":
    main()
