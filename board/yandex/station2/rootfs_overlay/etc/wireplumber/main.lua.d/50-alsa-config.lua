alsa_monitor.rules = {
  {
    matches = {
      {
        { "node.name", "matches", "alsa_output.platform-auge_sound.*" },
      },
    },
    apply_properties = {
      ["session.suspend-timeout-seconds"] = 0,
      ["audio.format"] = "S16LE",
      ["audio.rate"] = 48000,
      ["audio.channels"] = 2,
      ["audio.position"] = "FL,FR",
    },
  },
  {
    matches = {
      {
        { "node.name", "matches", "alsa_output.*" },
      },
    },
    apply_properties = {
      ["session.suspend-timeout-seconds"] = 0,
      ["audio.format"] = "S16LE",
      ["audio.rate"] = 48000,
      ["audio.channels"] = 2,
    },
  },
}
