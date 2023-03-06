#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

function exit_if_error {
  if [ $1 -ne 0 ]; then
    echo "ERROR: $2: retval=$1" >&2
    exit $1
  fi
}

# Set KLEAF=1 to build with kleaf. This ignores all other command line options.
if [ -n "${KLEAF}" ]; then
  exec tools/bazel run --config=akita --config=fast //private/devices/google/akita:zuma_akita_dist
fi

cat <<- EOF
==================== NOTICE ==============================
build.sh is going to be deprecated soon. All android14
kernels are moving to kleaf. Please migrate to using kleaf
by using the following command:

  KLEAF=1 $0

For any issues, file a bug using go/kleaf-bug.
==========================================================
EOF

export GKI_KERNEL_DIR=${GKI_KERNEL_DIR:-"aosp-staging"}
export KLEAF_SUPPRESS_BUILD_SH_DEPRECATION_WARNING=1
export LTO=${LTO:-thin}
export BUILD_CONFIG=private/devices/google/akita/build.config.akita
export VENDOR_DLKM_ETC_FILES=private/devices/google/akita/insmod_cfg/*

# TODO(b/239987494): Remove this when the core GKI fragment is removed.
# Since we can't have two build config fragments, any alternative use
# of GKI_BUILD_CONFIG_FRAGMENT must include CORE_GKI_FRAGMENT,
# i.e. `source ${CORE_GKI_FRAGMENT}`.
export CORE_GKI_FRAGMENT=private/devices/google/akita/build.config.akita.gki.fragment

if [ -z "${GKI_PREBUILTS_DIR}" ]; then
  GKI_BUILD_CONFIG_FRAGMENT=${GKI_BUILD_CONFIG_FRAGMENT:-${CORE_GKI_FRAGMENT}} \
  GKI_BUILD_CONFIG="${GKI_KERNEL_DIR}/build.config.gki.aarch64" \
    build/build.sh "$@"
else
  build/build.sh "$@"
fi

exit_if_error $? "Failed to create mixed build"
