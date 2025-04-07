#pragma once

#include "Noa.h"
#include <deque>

//
//  |----------------------------------|----------------------|
//  |--|-----|------------|------------|----------------|-----|
//     |--|--|            |---------|--|-------|-----|--|
//                        |------|--|  |-----|-|--|--|
//
//
class NoaContainer : public Noa
{
 private:
  std::deque<std::unique_ptr<Noa>> children_;

 protected:
  void print_real(std::ostream& os) const final;

 public:
  NoaContainer() : Noa(container) { }
};
