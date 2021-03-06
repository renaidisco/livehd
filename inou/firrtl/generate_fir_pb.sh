#!/bin/bash
#WARNING! This is a experimental and hardcoded script for Sheng-Hong environment only

pts_todo='Xor10k'
pts='GCD_3bits GCD Trivial TrivialArith Test1 Test2 Test3 Test4 Test5 Test6 SimpleBitOps Register RegisterSimple Flop
     BundleConnect MemoryController PlusAnd SubModule RWSmem Smem Blackbox Blackbox2'
# pts='GCD_3bits'

CHISEL_PATH=~/chisel/
CHISEL_RESULT_PATH=~/chisel/results/
CHISEL_SRC_PATH=~/chisel/src/main/scala/design/
FIRRTL_EXE=~/firrtl/utils/bin/firrtl
LIVEHD_FIRRTL_PATH=~/livehd/inou/firrtl/tests

cd $CHISEL_PATH
for pt in $pts
do
  sbt "runMain chisel3.stage.ChiselMain --module design.${pt}"  

  $FIRRTL_EXE -i ${pt}.fir -X verilog
  # $FIRRTL_EXE -i ${pt}.fir -X high
  $FIRRTL_EXE -i ${pt}.fir -X low
  # $FIRRTL_EXE -i ${pt}.fir -X none --custom-transforms firrtl.transforms.WriteChPB
  # $FIRRTL_EXE -i ${pt}.fir -X none --custom-transforms firrtl.transforms.WriteHighPB
  $FIRRTL_EXE -i ${pt}.fir -X none --custom-transforms firrtl.transforms.WriteLowPB

  mkdir -p $CHISEL_RESULT_PATH${pt}
  cp -f $CHISEL_SRC_PATH${pt}.scala $LIVEHD_FIRRTL_PATH/chisel
  mv -f ${pt}*.fir $LIVEHD_FIRRTL_PATH/firrtl
  mv -f ${pt}*.pb  $LIVEHD_FIRRTL_PATH/proto
  mv -f ${pt}.v   $LIVEHD_FIRRTL_PATH/verilog_gld/${pt}.gld.v
done

