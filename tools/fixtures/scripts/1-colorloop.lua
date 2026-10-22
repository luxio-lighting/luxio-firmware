function setup() end
function loop()
  local count = luxio_get_pixel_count()
  local time = luxio_millis() / 20000
  for i = 0, count - 1 do
    local h = (time) % 1 * 6
    local sector = math.floor(h)
    local up = math.floor((h - sector) * 255)
    local down = 255 - up
    local r, g, b = 0, 0, 0
    if sector == 0 then r, g = 255, up
    elseif sector == 1 then r, g = down, 255
    elseif sector == 2 then g, b = 255, up
    elseif sector == 3 then g, b = down, 255
    elseif sector == 4 then r, b = up, 255
    else r, b = 255, down end
    luxio_set_pixel_color(i, r, g, b)
  end
end