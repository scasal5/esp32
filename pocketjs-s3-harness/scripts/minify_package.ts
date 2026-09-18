// Use upstream's codec; preserve the manifest, profile, plan and asset sections.
import { decodePocketPackage, encodePocketPackage, POCKET_SECTION } from '../.deps/pocketjs/contracts/spec/pocket-package.ts';
import { createHash } from 'node:crypto';
const path = Bun.argv[2];
if (!path) throw Error('usage: bun minify_package.ts file.pocket');
const original = new Uint8Array(await Bun.file(path).arrayBuffer());
const pkg = decodePocketPackage(original);
const variants = await Promise.all(pkg.variants.map(async variant => ({...variant, sections:await Promise.all(variant.sections.map(async section => {
  if(section.kind !== POCKET_SECTION.js) return section;
  const source = new TextDecoder().decode(section.bytes.subarray(0,-1));
  const result=await Bun.build({entrypoints:['ws183-minify:guest'],target:'browser',format:'iife',minify:true,
    plugins:[{name:'guest-source',setup(build){
      build.onResolve({filter:/^ws183-minify:/},()=>({path:'guest',namespace:'ws183-minify'}));
      build.onLoad({filter:/.*/,namespace:'ws183-minify'},()=>({contents:source,loader:'js'}));
    }}]});
  if(!result.success)throw Error(result.logs.join('\n'));
  const js = new TextEncoder().encode(await result.outputs[0].text()+'\0');
  return {...section,bytes:js};
}))})));
const output=encodePocketPackage({...pkg,variants});
decodePocketPackage(output); // Verify the encoded checksums before replacing it.
await Bun.write(path,output);
const hash=(bytes:Uint8Array)=>createHash('sha256').update(bytes).digest('hex');
const jsBytes=(pkg:{variants:{sections:{kind:number,bytes:Uint8Array}[]}[]})=>{
  const section=pkg.variants[0]?.sections.find(s=>s.kind===POCKET_SECTION.js);
  if(!section)return 0;
  return section.bytes.length && section.bytes[section.bytes.length-1]===0 ? section.bytes.length-1 : section.bytes.length;
};
const originalPkg=decodePocketPackage(original);
await Bun.write(path+'.receipt.json',JSON.stringify({
  bun:Bun.version,transform:'minify',profileId:'ws183-harness',
  packageBytes:output.length,packageSha256:hash(output),
  javascriptBytes:jsBytes(decodePocketPackage(output)),
  originalPackageBytes:original.length,originalPackageSha256:hash(original),
  originalJavascriptBytes:jsBytes(originalPkg),
  originalMissing:false,recovered:false,
},null,2)+'\n');
console.log(`Minified package: ${original.length} -> ${output.length} bytes`);
