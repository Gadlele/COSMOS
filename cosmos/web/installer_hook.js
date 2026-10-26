/* COSMOS's install page: "Check the FM-1" (step 3), with the installer's own port finding and handshake (this is
 * added to the end of its script by tools/make_install.py, so updater, showPorts and writing are in scope) */
{
  const btn = document.getElementById("check"), out = document.getElementById("checkstatus");
  /* what the identity says the FM-1 runs */
  const runs = (id) => {
    const txt = id.text || "";
    let m = /^FM-1_72(\d\d)(\d\d)(\d\d)$/.exec(txt);
    if (m) return `COSMOS ${+m[1]}.${+m[2]}${+m[3] ? "." + +m[3] : ""}`;
    if (/^FM-1_72\d*$/.test(txt)) return "a COSMOS test build";
    m = /^FM-1_0(\d\d)$/.exec(txt);
    if (m && txt !== "FM-1_099") return `M-VAVE's firmware V${+m[1]}`;
    if (id.open) return `M-VAVE's Open firmware (${txt})`;
    if (/^ota-/i.test(id.model || "")) return "its update mode";
    return `another firmware (${txt})`;
  };
  if (btn) btn.addEventListener("click", async () => {
    if (writing) return;
    btn.disabled = true;
    out.className = "checked";
    out.textContent = "Looking for the FM-1…";
    let midi = null;
    try { midi = await navigator.requestMIDIAccess({ sysex: true }); }
    catch (_) {
      out.className = "checked bad";
      out.textContent = navigator.requestMIDIAccess
        ? "MIDI with SysEx was not allowed. Allow it in the site settings (left of the address bar) and check again."
        : "This browser has no Web MIDI. Open this page in Chrome or Edge.";
      btn.disabled = false;
      return;
    }
    let dev = null;
    try { dev = await updater(midi).find(null); } catch (_) { /* not found */ }
    if (dev) {
      dev.link.close();
      out.className = "checked ok";
      out.textContent = id_line(dev);
      $("portpick").hidden = true;
    } else {
      out.className = "checked bad";
      out.textContent = "FM-1 not found. Check that it is on and the cable carries data, close other apps and tabs that use MIDI, and check again. If it shows below under another name, press Try.";
      showPorts(midi);
    }
    btn.disabled = false;
  });
  function id_line(dev) {
    const what = runs(dev.id);
    const note = dev.id.open ? " Put M-VAVE's standard firmware back first (last card), then install COSMOS."
      : " Ready to install.";
    return `Found it on "${dev.name}": it runs ${what}.${note}`;
  }
}
