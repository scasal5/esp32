/// <reference path="./battery.d.ts" />
// This harness measures the native runtime, without a UI reconciler.
import { PROP, ENUMS, ROOT_ID, NODE_TYPE } from '../.deps/pocketjs/contracts/spec/spec';
import type { HostOps } from '../.deps/pocketjs/framework/src/host';

type Frame=(buttons:number,analog:number,touches?:number[])=>void;
const runtime=globalThis as unknown as {ui:HostOps;frame:Frame};
const ui=runtime.ui;
if(battery.apiVersion!==1)throw Error('ws183 battery API v1 required');
if(ui.__host!=='ws183-harness'||ui.__hostAbi!==1)throw Error('host mismatch');
const dark=0xff170f08,blue=0xffdc6825,white=0xffffffff;
ui.setProp(ROOT_ID,PROP.bgColor,dark);
ui.setProp(ROOT_ID,PROP.overflow,ENUMS.Overflow.Hidden);
function box(parent:number,x:number,y:number,w:number,h:number,color=0,type:number=NODE_TYPE.view):number {
  const id=ui.createNode(type);
  ui.setProp(id,PROP.posType,ENUMS.PosType.Absolute);
  ui.setProp(id,PROP.insetL,x);ui.setProp(id,PROP.insetT,y);
  ui.setProp(id,PROP.width,w);ui.setProp(id,PROP.height,h);
  if(color)ui.setProp(id,PROP.bgColor,color);
  ui.insertBefore(parent,id,0);return id;
}
function text(parent:number,y:number,value:string):number {
  const id=box(parent,8,y,224,24,0,NODE_TYPE.text);
  ui.setProp(id,PROP.fontSlot,2);ui.setProp(id,PROP.textColor,white);
  ui.setText(id,value);return id;
}
const scenes=Array.from({length:6},()=>box(ROOT_ID,0,0,240,284));
const names=['Static','Counter','Battery','Interrupt','List','Full screen'];
for(let i=0;i<6;i++){
  ui.setProp(scenes[i],PROP.display,ENUMS.Display.None);
  text(scenes[i],12,names[i]);
}
text(scenes[0],72,'Stable pixels');
box(scenes[1],0,64,240,64,blue);
const counter=text(scenes[1],84,'Count 0');
const batteryLabel=text(scenes[2],84,'No PMU');
const moving=box(scenes[3],0,80,48,48,blue);
const clip=box(scenes[4],0,64,240,192);
ui.setProp(clip,PROP.overflow,ENUMS.Overflow.Hidden);
const list=box(clip,0,0,240,720);
for(let i=0;i<20;i++)text(list,i*36,`Item ${i+1}`);
const full=box(scenes[5],0,0,240,284,blue);
const edge=box(ROOT_ID,0,0,1,1,0xff4444ff);
ui.setProp(edge,PROP.display,ENUMS.Display.None);
const edges=[[0,0,1,40],[239,0,1,40],[0,283,1,1],[239,283,1,1],
  [20,39,31,40],[21,40,31,41],[0,240,240,40],[1,241,239,43]];
let selected=-1,tick=0,count=0,position=0,target=0,scroll=0;
let held=false,previousY:number|undefined,batteryText='';
runtime.frame=(buttons,_analog,touches)=>{
  globalThis.__wsResponded=false;
  tick++;
  const next=globalThis.__wsScenario??0;
  if(next!==selected){
    if(selected>=0)ui.setProp(scenes[selected],PROP.display,ENUMS.Display.None);
    selected=next;ui.setProp(scenes[selected],PROP.display,ENUMS.Display.Flex);
    ui.setProp(edge,PROP.display,selected===3&&globalThis.__wsGolden?ENUMS.Display.Flex:ENUMS.Display.None);
    previousY=undefined;
  }
  const touch=touches?.[0];
  const x=touch===undefined?undefined:touch&511;
  const y=touch===undefined?undefined:(touch>>>9)&511;
  const down=touch!==undefined||(buttons&0x2000)!==0;
  if(selected===1&&down&&!held){ui.setText(counter,`Count ${++count}`);globalThis.__wsResponded=true;}
  else if(selected===1&&tick%30===0)ui.setText(counter,`Count ${++count}`);
  held=down;
  if(selected===2&&tick%90===0){
    const b=battery.read();
    const value=b.available?`${b.percent??'--'}%  ${b.millivolts??'--'} mV`:'No PMU';
    if(value!==batteryText){batteryText=value;ui.setText(batteryLabel,value);}
  }
  if(selected===3){
    if(x!==undefined)target=Math.max(0,Math.min(176,x-24));
    else if(tick%90===0)target=target?0:176;
    const p=Math.round(position+(target-position)*0.25);
    if(p!==position){position=p;ui.setProp(moving,PROP.translateX,p);globalThis.__wsResponded=x!==undefined;}
    if(globalThis.__wsGolden){
      const [left,top,w,h]=edges[tick%edges.length];
      ui.setProp(edge,PROP.insetL,left);ui.setProp(edge,PROP.insetT,top);
      ui.setProp(edge,PROP.width,w);ui.setProp(edge,PROP.height,h);
    }
  }
  if(selected===4){
    if(y!==undefined&&previousY!==undefined){
      scroll=Math.max(0,Math.min(528,scroll+previousY-y));ui.setProp(list,PROP.translateY,-scroll);
      globalThis.__wsResponded=y!==previousY;
    }
    else if(y===undefined&&tick%2===0){scroll=(scroll+2)%529;ui.setProp(list,PROP.translateY,-scroll);}
    previousY=y;
  }
  if(selected===5)ui.setProp(full,PROP.bgColor,0xff000000|((tick%240)<<16)|0x6825);
};
