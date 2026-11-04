import { expect, it } from 'vitest';
import { cppStrings } from './i18n-test-utils';
it('extracts C++ strings through comments, characters, escapes and custom raw delimiters', () => {
  expect(cppStrings(`// "忽略"
    /* R"(忽略)" */ auto c = '\\"'; auto apostrophe = '\\''; auto n = 1'000;
    auto a = u8"中文\\n\\"引号\\""; auto b = LR"tag(原文 // /* " )tag";
    auto u = "\\u4e2d";`)).toEqual(['中文\n"引号"', '原文 // /* " ', '中']);
});
