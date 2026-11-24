-- Garib Island: counts each player's garibs on the island and says who reaches the exit first.
local TOTAL = 11
local EXIT_X, EXIT_Z = 18, 600
local start = {}  -- each player's garibs when they came into the level
local shown = {}  -- the count each player last saw
local winner = nil

function on_frame()
  local f = game.frame()
  for p = 0, 15 do
    if game.has_player(p) then
      local g = game.garibs(p)
      if start[p] == nil then
        start[p] = g
        shown[p] = 0
        game.show(p, "Garib Island: find the " .. TOTAL .. " garibs", 150)
      end
      local got = g - start[p]
      if got ~= shown[p] and got >= 0 then
        shown[p] = got
        if got >= TOTAL then
          game.show(p, "All the garibs! Take the ball to the exit", 150)
        else
          game.show(p, "Garibs: " .. got .. " of " .. TOTAL, 90)
        end
      end
      if winner == nil then
        local x, y, z = game.pos(p)
        local dx, dz = x - EXIT_X, z - EXIT_Z
        if dx * dx + dz * dz < 40 * 40 then
          winner = p
          for q = 0, 15 do
            if game.has_player(q) then
              game.show(q, "Player " .. (p + 1) .. " reached the exit first!", 150)
            end
          end
        end
      end
    end
  end
end

function on_join(p)
  start[p] = nil
end
