#pragma once
// 列表界面：菜单、书库、目录都用它。只画看得见的那一页，几千章的目录也不占内存。
// 按键：上一页 / 下一页移动光标（长按翻一屏），菜单键确定，长按菜单返回。
#include <Arduino.h>

#include <functional>
#include <vector>

#include "app/app.h"

struct ListRow {
  String label;
  String value;     // 靠右显示（"18"、"开"、"37%"）
  bool marked = false;  // 前面画个圆点（当前的书、当前章）
};

class ListView : public View {
 public:
  explicit ListView(const String &title) : title_(title) {}
  void draw() override;
  void onKey(Key k, Press p) override;

 protected:
  virtual int count() const = 0;
  virtual void row(int i, ListRow &r) const = 0;
  virtual void select(int i) = 0;
  virtual String titleRight() const { return ""; }
  virtual const char *hint() const { return "翻页键选择　菜单键确定　长按菜单返回"; }
  int visibleRows() const;
  void setCursor(int c);
  String title_;
  int cursor_ = 0;
};

// 固定几项的菜单：每项一个动作；值会变的项在 build() 里重算
struct MenuItem {
  String label;
  std::function<String()> value;  // 可以为空
  std::function<void()> action;
};

class MenuView : public ListView {
 public:
  explicit MenuView(const String &title) : ListView(title) {}
  void onShow() override { refresh(); }

 protected:
  virtual void build(std::vector<MenuItem> &items) = 0;
  int count() const override { return items_.size(); }
  void row(int i, ListRow &r) const override;
  void select(int i) override;
  void refresh();  // 重建菜单项（设置改了以后）
  std::vector<MenuItem> items_;
};
