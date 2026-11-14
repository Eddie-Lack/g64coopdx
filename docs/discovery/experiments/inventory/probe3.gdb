set pagination off
set confirm off
break main.c:578
commands
silent
printf "HEAP static=%d dynamic=%d size=%d last=%d first=%d\n", jallocControl.staticUsed, jallocControl.dynamicUsed, jallocControl.heapSize, jallocControl.lastStatic, jallocControl.firstDynamic
continue
end
run
