function setup() end
function loop()
  for i = 0, luxio_get_pixel_count() - 1 do
    local t = 0.35 + 0.65 * (math.sin(i / 8 + luxio_millis() / 1800) + 1) / 2
    luxio_set_pixel_color(i, math.floor(15 * t), math.floor(140 * t), math.floor(255 * t))
  end
end