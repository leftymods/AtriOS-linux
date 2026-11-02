#!/bin/bash
# ==============================================================================
# AtriOS Build System - General Utility Functions
# ==============================================================================

BOLD="\033[1m"
RED="\033[1;31m"
GREEN="\033[1;32m"
YELLOW="\033[1;33m"
BLUE="\033[1;34m"
CYAN="\033[1;36m"
NC="\033[0m"

display_alert() {
    local level="${1:-INFO}"
    local message="${2:-}"
    local detail="${3:-}"

    case "$level" in
        "OK"|"SUCCESS")
            echo -e "${GREEN}[✔]${NC} ${BOLD}${message}${NC} ${detail}"
            ;;
        "INFO")
            echo -e "${BLUE}[*]${NC} ${BOLD}${message}${NC} ${detail}"
            ;;
        "WARN"|"WARNING")
            echo -e "${YELLOW}[!]${NC} ${BOLD}${message}${NC} ${detail}"
            ;;
        "ERROR"|"FAIL")
            echo -e "${RED}[✘]${NC} ${BOLD}${message}${NC} ${detail}"
            ;;
        *)
            echo -e "${CYAN}[•]${NC} ${message} ${detail}"
            ;;
    esac
}

exit_with_error() {
    display_alert "ERROR" "$1" "${2:-}"
    exit 1
}

check_tool() {
    local tool="$1"
    if ! command -v "$tool" &>/dev/null; then
        exit_with_error "Required tool missing: $tool" "Please install it via apt."
    fi
}
