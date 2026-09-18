"""Reviewed port of upstream immutable checks to Registry 0.14.0's actual source.

Upstream ed99509 expects hash 8779a505... and two-space formatting. Registry
0.14.0 ships 36128da1... with four-space formatting. Accept ONLY the reviewed
identity below. Apply the same reverse/species guards to a build-directory copy.
Neither the checkout nor managed_components is rewritten.
Original patch: pocket-stack/pocketjs (MIT), tools/prepare_quickjs.py.
"""
import hashlib
from pathlib import Path
import re
import sys

SOURCE_SHA256='36128da188cb236ffd029dd3c672ff8f85e5a196a9211e267a515c8efc1ab52c'


def prepare(source):
    if hashlib.sha256(source).hexdigest()!=SOURCE_SHA256:
        raise ValueError('Unreviewed QuickJS source identity; refusing to patch')
    text=source.decode()
    start=text.index('static JSValue js_typed_array_reverse(')
    end=text.index('\nstatic ',start+1)
    body=text[start:end]
    needle='    if (len > 0) {'
    if body.count(needle)!=1:raise ValueError('reverse shape changed')
    body=body.replace(needle,'    if (typed_array_is_immutable(JS_VALUE_GET_OBJ(this_val))) {\n'
        '        return JS_ThrowTypeErrorImmutableArrayBuffer(ctx);\n    }\n'+needle,1)
    text=text[:start]+body+text[end:]
    text,count=re.subn(r'(static JSValue js_typed_array___speciesCreate\([\s\S]*?JSValueConst \*argv)\)',r'\1, bool writable)',text)
    if count!=2:raise ValueError('species declaration count changed')
    for argc,writable,count in ((2,'true',3),(4,'false',1)):
        old=f'js_typed_array___speciesCreate(ctx, JS_UNDEFINED, {argc}, args)'
        if text.count(old)!=count:raise ValueError('species call count changed')
        text=text.replace(old,f'js_typed_array___speciesCreate(ctx, JS_UNDEFINED, {argc}, args, {writable})')
    start=text.rindex('static JSValue js_typed_array___speciesCreate(')
    end=text.index('\nstatic ',start+1)
    body=text[start:end]
    if body.count('    return ret;')!=1:raise ValueError('species return shape changed')
    body=body.replace('    return ret;','    if (writable && !JS_IsException(ret) &&\n'
        '        typed_array_is_immutable(JS_VALUE_GET_OBJ(ret))) {\n'
        '        JS_FreeValue(ctx, ret);\n'
        '        return JS_ThrowTypeErrorImmutableArrayBuffer(ctx);\n    }\n    return ret;')
    return text[:start]+body+text[end:]


if __name__=='__main__':
    result=prepare(Path(sys.argv[1]).read_bytes())
    output=Path(sys.argv[2]);output.parent.mkdir(parents=True,exist_ok=True)
    if not output.exists() or output.read_text()!=result:output.write_text(result,encoding='utf-8',newline='\n')
