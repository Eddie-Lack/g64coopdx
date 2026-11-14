set pagination off
set confirm off
break main.c:578
commands
silent
printf "FRAME objm=%d glistbytes=%d cur=%d vtx0=%d vtx1=%d mstack=%d\n", objectMatrix, actualGFXListLength[currentTask], currentTask, (*(unsigned*)&vtxPtr - (unsigned)&objectsVtx[0][0])/16, (*(unsigned*)&vtxPtr - (unsigned)&objectsVtx[1][0])/16, matrixStackCount
continue
end
run
