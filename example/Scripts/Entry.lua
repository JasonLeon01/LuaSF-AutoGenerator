-- Create a window
local mode = sf.VideoMode.new(sf.Vector2u.new(800, 600))
local ctx = sf.ContextSettings.new()
ctx.antiAliasingLevel = 8
local window = sf.RenderWindow.new(mode, "LuaSF Test", sf.Style.Titlebar | sf.Style.Close, sf.State.Windowed, ctx)

-- Set the background color
local bg = sf.Color.new(50, 50, 50)

-- Create a green circle
local circle = sf.CircleShape.new(80)
circle:setFillColor(sf.Color.new(0, 220, 80))
circle:setPosition(sf.Vector2f.new(320, 220))

-- Main loop
while window:isOpen() do
    -- Process all pending events
    while true do
        local event = window:pollEvent()
        if event == nil then
            break
        end
        if event:isClosed() then
            window:close()
            break
        end
    end

    window:clear(bg)
    window:draw(circle)
    window:display()
end
