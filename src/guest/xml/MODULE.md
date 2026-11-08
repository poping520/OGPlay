# 模块：BootDex SAX 适配器

复用 API19 KXml 解析有显式 byte stream 或 character stream 的文本 XML。字符流优先，
由解析器关闭选中的流。SAX 回调和属性采用原版 SAX 类型，namespace 默认开启。
支持文档、命名空间、元素和文本事件。解析错误为 SAXParseException，IO 与回调异常透传。
DTD 和仅指定 system ID 的输入明确失败；不支持验证、外部实体或处理指令事件。
