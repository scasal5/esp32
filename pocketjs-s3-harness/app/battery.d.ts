interface BatterySnapshot {
  readonly available: boolean;
  readonly present: boolean;
  readonly percent: number | null;
  readonly millivolts: number | null;
  readonly charging: boolean;
  readonly vbus: boolean;
  readonly ageMs: number;
}
/** Private ws183 extension. Deliberately absent from PocketJS capabilities. */
declare var battery: { readonly apiVersion: 1; read(): BatterySnapshot };
declare var __wsScenario: number | undefined;
