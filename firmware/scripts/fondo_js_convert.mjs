import { readFileSync, writeFileSync } from 'fs';

const W=240,H=284,MAX=1048576,MIN_CS=7,PAL_N=128,BG=10,DIM=204/255;

function coverNearest(src, sw, sh){
  const scale=Math.max(W/sw, H/sh);
  const nw=Math.max(W, Math.round(sw*scale));
  const nh=Math.max(H, Math.round(sh*scale));
  const left=Math.floor((nw-W)/2), top=Math.floor((nh-H)/2);
  const out=new Uint8ClampedArray(W*H*4);
  for(let y=0;y<H;y++){
    const sy=Math.min(sh-1, Math.floor((y+top)*sh/nh));
    for(let x=0;x<W;x++){
      const sx=Math.min(sw-1, Math.floor((x+left)*sw/nw));
      const si=(sy*sw+sx)*4, di=(y*W+x)*4;
      const a=src[si+3]/255;
      out[di]=src[si]*a+BG*(1-a);
      out[di+1]=src[si+1]*a+BG*(1-a);
      out[di+2]=src[si+2]*a+BG*(1-a);
      out[di+3]=255;
    }
  }
  return out;
}
function lzwDec(min, bytes){
  const clear=1<<min, eoi=clear+1;
  let size=min+1, next=eoi+1, dict=[];
  const out=[];
  let bp=0;
  const bit=()=>{const v=(bytes[bp>>3]>> (bp&7))&1; bp++; return v;};
  const code=()=>{let v=0; for(let i=0;i<size;i++) v|=bit()<<i; return v;};
  for(let i=0;i<clear;i++) dict[i]=[i];
  dict[clear]=[]; dict[eoi]=[];
  let prev=null, c;
  while(bp>>3 < bytes.length){
    c=code();
    if(c===eoi) break;
    if(c===clear){
      size=min+1; next=eoi+1; dict.length=next; prev=null; continue;
    }
    let entry;
    if(c<dict.length && dict[c]) entry=dict[c];
    else if(c===next && prev) entry=prev.concat(prev[0]);
    else break;
    for(let i=0;i<entry.length;i++) out.push(entry[i]);
    if(prev){
      dict[next]=prev.concat(entry[0]);
      next++;
      if(next===1<<size && size<12) size++;
    }
    prev=entry;
  }
  return out;
}
function lzwEnc(pix, min){
  const clear=1<<min, eoi=clear+1;
  let codeLen=min+1, next=eoi+1;
  const table=Object.create(null);
  const out=[];
  let pending=0, nbits=0;
  const emit=code=>{
    pending|=code<<nbits;
    nbits+=codeLen;
    while(nbits>=8){
      out.push(pending&255);
      pending>>>=8;
      nbits-=8;
    }
  };
  emit(clear);
  let cur=pix[0];
  for(let i=1;i<pix.length;i++){
    const k=pix[i], key=cur+','+k;
    if(table[key]!==undefined){cur=table[key]; continue;}
    emit(cur);
    if(next<4096){
      if(next>=(1<<codeLen) && codeLen<12) codeLen++;
      table[key]=next++;
    }else{
      emit(clear);
      for(const t in table) delete table[t];
      next=eoi+1;
      codeLen=min+1;
    }
    cur=k;
  }
  emit(cur);
  emit(eoi);
  if(nbits) out.push(pending&255);
  return out;
}
function parseGif(u){
  const gw=u[6]|u[7]<<8, gh=u[8]|u[9]<<8;
  const packed=u[10];
  let i=13;
  let gct=null;
  if(packed&0x80){
    const n=3*(1<<((packed&7)+1));
    gct=u.subarray(i,i+n); i+=n;
  }
  const frames=[];
  let delay=10, disp=0, trans=-1;
  while(i<u.length){
    const b=u[i];
    if(b===0x3B) break;
    if(b===0x21){
      const lab=u[i+1]; i+=2;
      if(lab===0xF9 && u[i]===4){
        const f=u[i+1]; disp=(f>>2)&7; trans=(f&1)?u[i+4]:-1;
        delay=u[i+2]|u[i+3]<<8; if(delay<MIN_CS) delay=MIN_CS;
      }
      while(i<u.length && u[i]!==0) i+=1+u[i];
      i++; continue;
    }
    if(b!==0x2C){i++; continue;}
    const left=u[i+1]|u[i+2]<<8, top=u[i+3]|u[i+4]<<8;
    const fw=u[i+5]|u[i+6]<<8, fh=u[i+7]|u[i+8]<<8;
    const ip=u[i+9]; i+=10;
    let lct=gct;
    if(ip&0x80){const n=3*(1<<((ip&7)+1)); lct=u.subarray(i,i+n); i+=n;}
    const min=u[i++]; const parts=[];
    while(i<u.length && u[i]!==0){const n=u[i++]; parts.push(u.subarray(i,i+n)); i+=n;}
    i++;
    let len=0; for(const p of parts) len+=p.length;
    const data=new Uint8Array(len); let o=0; for(const p of parts){data.set(p,o); o+=p.length;}
    let idx=lzwDec(min, data);
    frames.push({left,top,w:fw,h:fh,idx,pal:lct,delay,disp,trans,gw,gh});
  }
  return {w:gw,h:gh,frames};
}
function compose(gif){
  const {w,h,frames}=gif;
  const buf=new Uint8ClampedArray(w*h*4);
  let prev=null;
  const out=[];
  for(const fr of frames){
    if(fr.disp===3) prev=buf.slice();
    const pal=fr.pal;
    const need=fr.w*fr.h;
    if(fr.idx.length<need) console.log('short lzw', fr.idx.length, need);
    for(let y=0;y<fr.h;y++){
      for(let x=0;x<fr.w;x++){
        const ci=fr.idx[y*fr.w+x];
        if(ci===fr.trans) continue;
        const p=((fr.top+y)*w+(fr.left+x))*4;
        if(p<0||p+3>=buf.length||!pal) continue;
        buf[p]=pal[ci*3]; buf[p+1]=pal[ci*3+1]; buf[p+2]=pal[ci*3+2]; buf[p+3]=255;
      }
    }
    out.push({delay:Math.max(fr.delay,MIN_CS), rgba:buf.slice(), w,h});
    if(fr.disp===2){
      for(let y=0;y<fr.h;y++){
        for(let x=0;x<fr.w;x++){
          const p=((fr.top+y)*w+(fr.left+x))*4;
          if(p>=0&&p+3<buf.length) buf[p]=buf[p+1]=buf[p+2]=buf[p+3]=0;
        }
      }
    }else if(fr.disp===3 && prev) buf.set(prev);
  }
  return out;
}
function medianCut(rgbs, colors){
  const boxes=[{px:rgbs}];
  const chRange=(box,ch)=>{let mn=255,mx=0; for(const p of box.px){const v=p[ch]; if(v<mn)mn=v; if(v>mx)mx=v;} return mx-mn;};
  while(boxes.length<colors){
    let bi=0, br=-1;
    for(let i=0;i<boxes.length;i++){
      if(boxes[i].px.length<2) continue;
      const r=Math.max(chRange(boxes[i],0), chRange(boxes[i],1), chRange(boxes[i],2));
      if(r>br){br=r; bi=i;}
    }
    if(br<=0) break;
    const box=boxes[bi];
    const rv=chRange(box,0), gv=chRange(box,1), bv=chRange(box,2);
    let ch=0; if(gv>=rv && gv>=bv) ch=1; else if(bv>=rv && bv>=gv) ch=2;
    box.px.sort((a,b)=>a[ch]-b[ch]);
    const mid=box.px.length>>1;
    boxes[bi]={px:box.px.slice(0,mid)};
    boxes.splice(bi+1,0,{px:box.px.slice(mid)});
  }
  const pal=new Uint8Array(colors*3);
  for(let i=0;i<boxes.length;i++){
    let r=0,g=0,b=0,n=boxes[i].px.length;
    for(const p of boxes[i].px){r+=p[0]; g+=p[1]; b+=p[2];}
    pal[i*3]=r/n; pal[i*3+1]=g/n; pal[i*3+2]=b/n;
  }
  return pal;
}
function palFromFrames(frames){
  const px=[];
  const take=Math.min(4, frames.length);
  for(let f=0;f<take;f++){
    const d=frames[f].rgba;
    for(let i=0;i<d.length;i+=4) px.push([d[i], d[i+1], d[i+2]]);
  }
  return medianCut(px, PAL_N);
}
function toIndex(rgba, pal){
  const n=PAL_N, idx=new Uint8Array(W*H);
  const near=new Int32Array(1<<12).fill(-1);
  for(let i=0,p=0;i<idx.length;i++,p+=4){
    const k=((rgba[p]&0xF0)<<4)|(rgba[p+1]&0xF0)|(rgba[p+2]>>4);
    let c=near[k];
    if(c<0){
      let best=1e9; c=0;
      for(let j=0;j<n;j++){
        const dr=rgba[p]-pal[j*3], dg=rgba[p+1]-pal[j*3+1], db=rgba[p+2]-pal[j*3+2];
        const e=dr*dr+dg*dg+db*db; if(e<best){best=e;c=j;}
      }
      near[k]=c;
    }
    idx[i]=c;
  }
  return idx;
}
function encodeGif(frames, pal){
  const n=pal.length/3;
  const gctBits=Math.round(Math.log2(n))-1;
  const minCode=gctBits+1;
  const packed=0x80|0x70|gctBits;
  const out=[];
  const u8=v=>out.push(v&255);
  const u16=v=>{u8(v);u8(v>>8);};
  const s='GIF89a'; for(let i=0;i<6;i++) u8(s.charCodeAt(i));
  u16(W); u16(H); u8(packed); u8(0); u8(0);
  for(let i=0;i<pal.length;i++) u8(pal[i]);
  u8(0x21); u8(0xFF); u8(11);
  const ns='NETSCAPE2.0'; for(let i=0;i<11;i++) u8(ns.charCodeAt(i));
  u8(3); u8(1); u16(0); u8(0);
  for(const fr of frames){
    const idx=toIndex(fr.rgba, pal);
    u8(0x21); u8(0xF9); u8(4); u8(0); u16(fr.delay); u8(0); u8(0);
    u8(0x2C); u16(0); u16(0); u16(W); u16(H); u8(0); u8(minCode);
    const bits=lzwEnc(idx, minCode);
    for(let i=0;i<bits.length;){
      const chunk=Math.min(255, bits.length-i); u8(chunk);
      for(let j=0;j<chunk;j++) u8(bits[i++]);
    }
    u8(0);
  }
  u8(0x3B);
  return new Uint8Array(out);
}

const src = process.argv[2];
const dst = process.argv[3];
const u = readFileSync(src);
console.time('parse');
const g = parseGif(u);
console.timeEnd('parse');
console.log('src', g.w, g.h, 'frames', g.frames.length, 'idx0', g.frames[0].idx.length, 'expect', g.frames[0].w*g.frames[0].h);
console.time('compose');
let frames = compose(g);
console.timeEnd('compose');
console.time('scale');
frames = frames.map(fr=>({delay:fr.delay, rgba:coverNearest(fr.rgba, fr.w, fr.h)}));
console.timeEnd('scale');
/* Mismo atenuado que main/fondo_form.html y scripts/gif_to_panel.py. Sin esto
   este script saca un GIF mas claro que los otros dos y parece que el encoder
   se rompio. */
for(const fr of frames){
  const d=fr.rgba;
  for(let i=0;i<d.length;i+=4){
    d[i]=Math.round(d[i]*DIM+BG*(1-DIM));
    d[i+1]=Math.round(d[i+1]*DIM+BG*(1-DIM));
    d[i+2]=Math.round(d[i+2]*DIM+BG*(1-DIM));
  }
}
console.time('pal');
const pal = palFromFrames(frames);
console.timeEnd('pal');
console.time('enc');
const bin = encodeGif(frames, pal);
console.timeEnd('enc');
writeFileSync(dst, bin);
console.log('out bytes', bin.length, 'packed', (0x80|0x70|(Math.round(Math.log2(PAL_N))-1)).toString(16));
