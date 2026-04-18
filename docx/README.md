# 2K0300 文档索引

本目录集中放置当前仓库的环境与使用文档。

## 文档列表

- [ENV_SETUP_WSL_BOARD.md](/home/madejuele/projects/2K0300/docx/ENV_SETUP_WSL_BOARD.md)
  - 已完成项已用 `[x]` 标记
  - 这是当前已经实际跑通并验证过的环境记录
- [guide.md](/home/madejuele/projects/2K0300/docx/guide.md)
  - 已补充“当前完成标记”
  - 这是较完整的 WSL 版操作说明，包含未在本轮重新验证的扩展步骤

## 当前结论

已完成并验证：
- [x] 串口接入 WSL
- [x] 龙芯 LoongArch 交叉编译工具链安装
- [x] OpenCV 预编译库安装
- [x] SSH / `scp -O` 到板卡
- [x] Wi-Fi 重启 panic 修复
- [x] 用户工程本地交叉编译
- [x] 用户工程上传到板卡

暂未在本轮重新验证：
- [ ] 内核重新编译
- [ ] 新内核上传与刷写
- [ ] 设备树修改流程
