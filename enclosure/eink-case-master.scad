// 手板版入口：光固化或 CNC 手板，之后翻硅胶模复模（PU 真空注型）。打开这个文件，F6 渲染、F7 导出。
// 默认 part = "print"：中框、后盖、键帽按打印方向排好的一个文件，发给 Claude 检查后再拆成三个零件文件给手板厂。
// 模型都在 eink-case.scad，这里只选版本。
include <eink-case.scad>
VARIANT = "master";
part = "print";
