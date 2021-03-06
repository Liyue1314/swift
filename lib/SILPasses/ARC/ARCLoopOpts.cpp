//===--- ARCLoopOpts.cpp --------------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
///
/// \file
///
/// This is a pass that runs multiple interrelated loop passes on a function. It
/// also provides caching of certain analysis information that is used by all of
/// the passes.
///
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "arc-sequence-opts"
#include "swift/SILPasses/Passes.h"
#include "GlobalARCPairingAnalysis.h"
#include "ProgramTerminationAnalysis.h"
#include "swift/SILAnalysis/AliasAnalysis.h"
#include "swift/SILAnalysis/DominanceAnalysis.h"
#include "swift/SILAnalysis/LoopAnalysis.h"
#include "swift/SILAnalysis/LoopRegionAnalysis.h"
#include "swift/SILAnalysis/RCIdentityAnalysis.h"
#include "swift/SILPasses/Transforms.h"

using namespace swift;

//===----------------------------------------------------------------------===//
//                              Top Level Driver
//===----------------------------------------------------------------------===//

namespace {

class ARCLoopOpts : public SILFunctionTransform {

  void run() override {
    auto *F = getFunction();

    // If ARC optimizations are disabled, don't optimize anything and bail.
    if (!getOptions().EnableARCOptimizations)
      return;

    // Skip global init functions.
    if (F->getName().startswith("globalinit_"))
      return;

    auto *LA = getAnalysis<SILLoopAnalysis>();
    auto *LI = LA->get(F);
    auto *DA = getAnalysis<DominanceAnalysis>();
    auto *DI = DA->get(F);

    // Canonicalize the loops, invalidating if we need to.
    if (canonicalizeAllLoops(DI, LI)) {
      // We preserve loop info and the dominator tree.
      DA->lockInvalidation();
      LA->lockInvalidation();
      PM->invalidateAnalysis(F, SILAnalysis::InvalidationKind::FunctionBody);
      DA->unlockInvalidation();
      LA->unlockInvalidation();
    }

    // Get all of the analyses that we need.
    auto *AA = getAnalysis<AliasAnalysis>();
    auto *RCFI = getAnalysis<RCIdentityAnalysis>()->get(F);
    auto *LRFI = getAnalysis<LoopRegionAnalysis>()->get(F);
    ProgramTerminationFunctionInfo PTFI(F);

    // Create all of our visitors, register them with the visitor group, and
    // run.
    LoopARCPairingContext LoopARCContext(*F, AA, LRFI, LI, RCFI, &PTFI);
    SILLoopVisitorGroup VisitorGroup(F, LI);
    VisitorGroup.addVisitor(&LoopARCContext);
    VisitorGroup.run();

    if (LoopARCContext.madeChange()) {
      invalidateAnalysis(SILAnalysis::InvalidationKind::CallsAndInstructions);
    }
  }

  StringRef getName() override { return "ARC Loop Opts"; }
};

} // end anonymous namespace

SILTransform *swift::createARCLoopOpts() {
  return new ARCLoopOpts();
}
