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

- [ ] string/vector/array output 与 guest pointer identity 测试通过；
- [ ] shader compiler/program/uniform lifecycle 使用真实 ANGLE error/result；
- [ ] 142 项 handler coverage gate、architecture gate 与 focused tests 通过。
