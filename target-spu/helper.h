DEF_HELPER_1(debug, void, env)

DEF_HELPER_FLAGS_1(clz, TCG_CALL_NO_RWG_SE, i32, i32)
DEF_HELPER_FLAGS_1(cntb, TCG_CALL_NO_RWG_SE, i32, i32)
DEF_HELPER_FLAGS_1(fsmb, TCG_CALL_NO_RWG_SE, i32, i32)
DEF_HELPER_FLAGS_1(fsmh, TCG_CALL_NO_RWG_SE, i32, i32)
DEF_HELPER_FLAGS_4(gbb, TCG_CALL_NO_RWG_SE, i32, i32, i32, i32, i32)
DEF_HELPER_FLAGS_4(gbh, TCG_CALL_NO_RWG_SE, i32, i32, i32, i32, i32)
DEF_HELPER_FLAGS_4(gb, TCG_CALL_NO_RWG_SE, i32, i32, i32, i32, i32)
DEF_HELPER_FLAGS_2(avgb, TCG_CALL_NO_RWG_SE, i32, i32, i32)
DEF_HELPER_FLAGS_2(absdb, TCG_CALL_NO_RWG_SE, i32, i32, i32)
DEF_HELPER_FLAGS_2(sumb, TCG_CALL_NO_RWG_SE, i32, i32, i32)
