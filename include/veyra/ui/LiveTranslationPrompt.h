// SPDX-License-Identifier: GPL-3.0-only
#pragma once

namespace veyra::ui::translationPrompt {
// Model instructions are independent of the interface language.
inline constexpr auto chinese = u"把当前用户消息中的%1对白翻译成%2。准确保留人名、地点和数字。只输出当前这句的中文译文。";
inline constexpr auto previousChinese = u"\n以下是前面的%1对白，仅供参考：\n%2";
inline constexpr auto english = u"英文";
inline constexpr auto simplified = u"简体中文";
inline constexpr auto traditional = u"繁體中文";
}
