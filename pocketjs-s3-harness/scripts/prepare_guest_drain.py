"""Stop Promise-job drain when the guest interrupt epoch advances.

Tiny jobs never poll JS_SetInterruptHandler, so a re-enqueue chain outlives
the frame budget. Check the epoch between jobs. Refuse unknown guest.c.
"""
from pathlib import Path
import sys

NEEDLE = """  while ((result = JS_ExecutePendingJob(guest->runtime, &context)) > 0) {
    guest->jobs++;
  }"""
PATCH = """  while ((result = JS_ExecutePendingJob(guest->runtime, &context)) > 0) {
    guest->jobs++;
    const unsigned int requested =
        atomic_load_explicit(&guest->interrupt_epoch, memory_order_relaxed);
    if (requested != guest->handled_interrupt_epoch) {
      guest->handled_interrupt_epoch = requested;
      return ESP_ERR_TIMEOUT;
    }
  }"""


def prepare(source: bytes) -> bytes:
    text = source.decode().replace("\r\n", "\n")
    if text.count(NEEDLE) != 1:
        raise ValueError("drain_jobs loop not unique; refusing to patch guest.c")
    return text.replace(NEEDLE, PATCH, 1).encode()


if __name__ == "__main__":
    result = prepare(Path(sys.argv[1]).read_bytes())
    output = Path(sys.argv[2])
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_bytes() != result:
        output.write_bytes(result)
