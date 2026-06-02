import json

with open("output/sfml_api.json", "r", encoding="utf-8") as f:
    data = json.load(f)

results = []

def walk_decl(decl, depth=0):
    """Recursively walk declarations looking for CLASS_DECL and STRUCT_DECL."""
    kind = decl.get("kind")
    if kind in ("CLASS_DECL", "STRUCT_DECL"):
        children = decl.get("children", [])
        field_names = []
        has_constructor = False
        for child in children:
            ckind = child.get("kind")
            if ckind == "FIELD_DECL":
                field_names.append(child.get("name"))
            elif ckind == "CONSTRUCTOR":
                has_constructor = True
        
        if field_names and not has_constructor:
            base_classes = decl.get("base_classes", [])
            results.append({
                "kind": kind,
                "qualified_name": decl.get("qualified_name"),
                "field_names": field_names,
                "has_base_classes": len(base_classes) > 0,
                "base_class_names": [b.get("name") for b in base_classes]
            })
    
    # Recurse into children
    for child in decl.get("children", []):
        walk_decl(child, depth + 1)

# The top level has a "files" array
for file_entry in data.get("files", []):
    for decl in file_entry.get("declarations", []):
        walk_decl(decl)

# Print results
print(f"Found {len(results)} types with FIELD_DECL(s) but no CONSTRUCTOR:\n")
for r in sorted(results, key=lambda x: x["qualified_name"]):
    suffix = ""
    if r["has_base_classes"]:
        suffix = f"  [bases: {', '.join(r['base_class_names'])}]"
    print(f"  {r['kind']:12s} {r['qualified_name']}{suffix}")
    for fn in r["field_names"]:
        print(f"    - {fn}")
    print()
