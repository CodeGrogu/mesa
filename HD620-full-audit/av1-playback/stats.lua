local function dump()
  local p = function(n) return tostring(mp.get_property(n)) end
  io.stderr:write(string.format("STAT frames=%s drop=%s decdrop=%s delayed=%s hwdec=%s codec=%s vo=%s ctx=%s\n",
    p("estimated-frame-number"), p("frame-drop-count"), p("decoder-frame-drop-count"),
    p("vo-delayed-frame-count"), p("hwdec-current"), p("video-codec"), p("current-vo"), p("current-gpu-context")))
end
mp.register_event("end-file", function() end)
mp.add_hook("on_unload", 50, dump)
