set pagination off
set confirm off
printf "ADDR hand=%u ball=%u\n", (unsigned)&hand, (unsigned)&ball
break DrawActor
commands
silent
printf "DA %u %d %d\n", (unsigned)ptr, objectMatrix, ptr->type
continue
end
run
