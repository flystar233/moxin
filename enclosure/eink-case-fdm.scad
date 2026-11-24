// FDM 版入口：自己的打印机，0.4 喷嘴 + PLA。打开这个文件，F6 渲染、F7 导出。
// 模型都在 eink-case.scad，这里只选版本；要导出别的零件改 part（print / keytest / frame / back / keys / assembly）。
include <eink-case.scad>
VARIANT = "fdm";
part = "print";
