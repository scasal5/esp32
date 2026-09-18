/// <reference path="./battery.d.ts" />
import { createSignal, For, Show } from 'solid-js';
import { mount } from '@pocketjs/framework/solid';
import { Text, View } from '@pocketjs/framework/solid/components';
import { onFrame } from '@pocketjs/framework/solid/lifecycle';
import { touches } from '@pocketjs/framework/input';

if (typeof battery === 'undefined' || battery.apiVersion !== 1) {
  throw new Error('ws183 battery API v1 required');
}

function App() {
  const [scenario, setScenario] = createSignal(0);
  const [count, setCount] = createSignal(0);
  const [level, setLevel] = createSignal('No PMU');
  const [position, setPosition] = createSignal(0);
  const [scroll, setScroll] = createSignal(0);
  let tick = 0, target = 0, previousY: number | undefined;
  const names = ['Static', 'Counter', 'Battery', 'Interrupt', 'List', 'Full screen'];
  onFrame(() => {
    tick++;
    const selected = globalThis.__wsScenario ?? 0;
    if (selected !== scenario()) {
      setScenario(selected); setPosition(0); setScroll(0); target = 0;
    }
    if (scenario() === 2 && tick % 90 === 0) {
      const b = battery.read();
      // No ageMs in the visual tree: it must not create per-tick damage.
      setLevel(b.available ? `${b.percent ?? '--'}%  ${b.millivolts ?? '--'} mV` : 'No PMU');
    }
    const contact = touches()[0];
    if (scenario() === 3) {
      if (contact) target = Math.max(0, Math.min(176, contact.x - 24));
      else if (tick % 90 === 0) target = target ? 0 : 176;
      const next = Math.round(position() + (target - position()) * 0.25);
      setPosition(next);
    }
    if (scenario() === 4) {
      if (contact && previousY !== undefined) setScroll(Math.max(0, Math.min(540, scroll() + previousY - contact.y)));
      previousY = contact?.y;
    }
    if (scenario() === 5) setPosition((position() + 8) % 240);
  });
  return (
    <View class="w-full h-full bg-slate-950 flex-col">
      <View class="h-16 items-center justify-center"><Text class="text-lg text-white">{names[scenario()]}</Text></View>
      <Show when={scenario() === 0}><Text class="text-white">Stable pixels</Text></Show>
      <Show when={scenario() === 1}>
        <View class="h-16 bg-blue-600 items-center justify-center" focusable onPress={() => setCount(count() + 1)}>
          <Text class="text-white">{`Count ${count()}`}</Text>
        </View>
      </Show>
      <Show when={scenario() === 2}>
        <View class="h-24 items-center justify-center"><Text class="text-white">{level()}</Text></View>
      </Show>
      <Show when={scenario() === 3}>
        <View class="w-12 h-12 bg-blue-600 rounded-lg" style={{ translateX: position() }} />
      </Show>
      <Show when={scenario() === 4}>
        <View class="h-48 overflow-hidden"><View class="flex-col" style={{ translateY: -scroll() }}>
          <For each={Array.from({ length: 20 }, (_, i) => i + 1)}>{i =>
            <View class="h-9"><Text class="text-white">{`Item ${i}`}</Text></View>
          }</For>
        </View></View>
      </Show>
      <Show when={scenario() === 5}>
        <View class="absolute top-0 left-0 w-full h-full bg-blue-600" style={{ translateX: position() }} />
      </Show>
    </View>
  );
}
mount(() => <App />);
