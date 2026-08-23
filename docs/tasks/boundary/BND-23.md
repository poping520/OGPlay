# BND-23 · GLES2 shader、uniform 与 vertex attribute 完整闭合

## 目标

完成 GLES2 剩余 26 项并证明 142 core 全部有真实 handler。

## 闭集

`glBindAttribLocation`、`glDetachShader`、`glGetAttachedShaders`、
`glGetShaderPrecisionFormat`、`glGetShaderSource`、`glGetUniformfv`、`glGetUniformiv`、
`glGetVertexAttribPointerv`、`glGetVertexAttribfv`、`glGetVertexAttribiv`、
`glReleaseShaderCompiler`、`glShaderBinary`、`glUniform2f`、`glUniform2i`、`glUniform3f`、
`glUniform3i`、`glUniform4i`、`glUniformMatrix2fv`、`glValidateProgram`、
`glVertexAttrib1f`、`glVertexAttrib1fv`、`glVertexAttrib2f`、`glVertexAttrib2fv`、
`glVertexAttrib3f`、`glVertexAttrib3fv`、`glVertexAttrib4fv`。

## 验收

- [x] string/vector/array output 与 guest pointer identity 测试通过；
- [x] shader compiler/program/uniform lifecycle 使用真实 ANGLE error/result；
- [x] 142 项 handler coverage gate、architecture gate 与 focused tests 通过。

## 结果

26 个剩余 export 全部由 `Gles2Module` compile-time direct binding 进入命名 handler；link
成功后从真实 ANGLE active-uniform metadata 登记 location value shape，relink/delete 清理旧
shape。vertex pointer/query 返回 guest logical descriptor 和原始 pointer/offset，constant value
跨 GLES1 fixed draw 恢复；`glShaderBinary` 保留真实 `glGetError`，不伪造支持。
