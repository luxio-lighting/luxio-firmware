function setup()
  local stops = {{255,110,20},{255,35,110},{105,30,255}}
  local count = luxio_get_pixel_count()
  for i = 0, count - 1 do
    local position = i / math.max(count - 1, 1) * (#stops - 1)
    local segment = math.min(math.floor(position) + 1, #stops - 1)
    local t = position - (segment - 1)
    local a, b = stops[segment], stops[segment + 1]
    luxio_set_pixel_color(i,
      math.floor(a[1] + (b[1] - a[1]) * t + 0.5),
      math.floor(a[2] + (b[2] - a[2]) * t + 0.5),
      math.floor(a[3] + (b[3] - a[3]) * t + 0.5))
  end
end
function loop() end