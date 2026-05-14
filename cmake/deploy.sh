#!/usr/bin/env bash
set -euo pipefail

timestamp() {
  date '+%Y-%m-%d %H:%M:%S'
}

log_info() {
  echo "[$(timestamp)] [INFO] $*"
}

log_warn() {
  echo "[$(timestamp)] [WARN] $*"
}

log_error() {
  echo "[$(timestamp)] [ERROR] $*" >&2
}

cleanup_tmp_dir() {
  if [[ -n "${tmp_dir:-}" && -d "${tmp_dir}" ]]; then
    log_info "Cleaning temporary directory: ${tmp_dir}"
    rm -rf "${tmp_dir}"
  fi
}

usage() {
  cat <<'EOF'
Usage:
  deploy.sh --zip <deployment-zip> [--target-dir <dir>] [--yes-overwrite-ini|--no-overwrite-ini] [--skip-install]

Description:
  Local deployment on the target host.
  - Unpacks the deployment zip.
  - Copies files to --target-dir using checksum-based skip for identical files.
  - Protects existing .ini files with prompt/allow/deny policy.
  - Restarts only services for components that actually received new files.

Options:
  --zip <file>              Deployment zip (required)
  --target-dir <dir>        Deployment target directory (default: ~/mqtt)
  --yes-overwrite-ini       Overwrite changed .ini files without prompt
  --no-overwrite-ini        Never overwrite changed .ini files
  --skip-install            Copy only, do not apply install/restart actions
  -h, --help                Show this help
EOF
}

require_command() {
  local cmd="$1"
  log_info "Checking required command: ${cmd}"
  if ! command -v "${cmd}" >/dev/null 2>&1; then
    log_error "Missing required command: ${cmd}"
    exit 1
  fi
}

calc_sha256() {
  local file_path="$1"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "${file_path}" | awk '{print $1}'
    return
  fi
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "${file_path}" | awk '{print $1}'
    return
  fi
  log_error "Neither sha256sum nor shasum is available."
  exit 1
}

is_component() {
  case "$1" in
    broker|filestore|msgstore|automation|valueservice|brokerconnector|httpmqttinterface|remoteservice)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

prompt_overwrite() {
  local rel_path="$1"
  while true; do
    read -r -p "Target protected config exists and differs: ${rel_path}. Overwrite? [y]es/[n]o/[a]ll/[s]kip-all: " answer
    answer="${answer,,}"
    case "${answer}" in
      y|n|a|s)
        echo "${answer}"
        return
        ;;
      *)
        echo "Please answer with y, n, a, or s."
        ;;
    esac
  done
}

install_journald_namespace_config() {
  local script_dir="$1"
  local namespace="$2"
  local sudo_cmd="$3"
  local src_conf="${script_dir}/journald/${namespace}.conf"
  local dropin_name="20-yaha-retention.conf"
  local target_dir="/etc/systemd/journald@${namespace}.conf.d"
  local target_conf="${target_dir}/${dropin_name}"

  if [[ ! -f "${src_conf}" ]]; then
    log_error "Missing journald namespace config: ${src_conf}"
    return 1
  fi

  log_info "Ensuring journald namespace dir: ${target_dir}"
  ${sudo_cmd} mkdir -p "${target_dir}"
  if [[ -f "${target_conf}" ]] && cmp -s "${src_conf}" "${target_conf}"; then
    log_info "Journald namespace config unchanged: ${namespace}"
    return 0
  fi

  log_info "Updating journald namespace config: ${namespace}"
  ${sudo_cmd} cp "${src_conf}" "${target_conf}"
  return 2
}

apply_journald_namespace_configs() {
  local script_dir="$1"
  local sudo_cmd="$2"
  local changed_any=0
  local rc=0
  local namespace

  for namespace in broker filestore msgstore autom valuesvc brkconn httpmqtt remotesvc; do
    if install_journald_namespace_config "${script_dir}" "${namespace}" "${sudo_cmd}"; then
      rc=0
    else
      rc=$?
      if [[ ${rc} -eq 2 ]]; then
        changed_any=1
      else
        return ${rc}
      fi
    fi
  done

  if [[ ${changed_any} -eq 1 ]]; then
    log_info "Reloading systemd daemon after journald namespace changes"
    ${sudo_cmd} systemctl daemon-reload
    for namespace in broker filestore msgstore autom valuesvc brkconn httpmqtt remotesvc; do
      log_info "Restarting systemd-journald namespace: ${namespace}"
      ${sudo_cmd} systemctl restart "systemd-journald@${namespace}.service" || true
    done
    log_info "Updated journald namespace policies."
  else
    log_info "Journald namespace policies unchanged."
  fi
}

install_nginx_config() {
  local script_dir="$1"
  local sudo_cmd="$2"
  local src_conf="${script_dir}/nginx/controlapp.conf"
  local target_conf="/etc/nginx/sites-available/controlapp"
  local enabled_link="/etc/nginx/sites-enabled/controlapp"
  local obsolete_link="/etc/nginx/sites-enabled/controlapp.conf"

  if [[ ! -f "${src_conf}" ]]; then
    log_warn "Skipping nginx setup (missing ${src_conf})."
    return 0
  fi

  log_info "Ensuring nginx directories and links"
  ${sudo_cmd} mkdir -p /etc/nginx/sites-available /etc/nginx/sites-enabled
  ${sudo_cmd} rm -f "${obsolete_link}"

  local changed=0
  local had_backup=0
  local backup_conf=""

  if [[ ! -f "${target_conf}" ]] || ! cmp -s "${src_conf}" "${target_conf}"; then
    changed=1
    if [[ -f "${target_conf}" ]]; then
      backup_conf="${target_conf}.bak.$(date +%Y%m%d%H%M%S)"
      log_info "Backing up existing nginx config: ${backup_conf}"
      ${sudo_cmd} cp "${target_conf}" "${backup_conf}"
      had_backup=1
    fi
    log_info "Installing nginx controlapp config"
    ${sudo_cmd} cp "${src_conf}" "${target_conf}"
  fi

  ${sudo_cmd} ln -sfn "${target_conf}" "${enabled_link}"

  if ! ${sudo_cmd} nginx -t; then
    log_error "nginx -t failed, rolling back controlapp.conf"
    if [[ ${had_backup} -eq 1 ]]; then
      ${sudo_cmd} cp "${backup_conf}" "${target_conf}"
    elif [[ ${changed} -eq 1 ]]; then
      ${sudo_cmd} rm -f "${target_conf}"
      ${sudo_cmd} rm -f "${enabled_link}"
    fi
    ${sudo_cmd} nginx -t || true
    return 1
  fi

  if [[ ${changed} -eq 1 ]]; then
    log_info "Reloading nginx"
    ${sudo_cmd} systemctl reload nginx
    log_info "Installed nginx controlapp.conf and reloaded nginx."
  else
    log_info "nginx controlapp.conf unchanged; no reload needed."
  fi
}

zip_file=""
target_dir="~/mqtt"
overwrite_mode="ask"
skip_install=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --zip)
      if [[ $# -lt 2 ]]; then
        log_error "--zip requires a value"
        exit 1
      fi
      zip_file="$2"
      shift 2
      ;;
    --target-dir)
      if [[ $# -lt 2 ]]; then
        log_error "--target-dir requires a value"
        exit 1
      fi
      target_dir="$2"
      shift 2
      ;;
    --yes-overwrite-ini)
      overwrite_mode="all"
      shift
      ;;
    --no-overwrite-ini)
      overwrite_mode="none"
      shift
      ;;
    --skip-install)
      skip_install=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      log_error "Unknown argument: $1"
      usage >&2
      exit 1
      ;;
  esac
done

if [[ -z "${zip_file}" ]]; then
  log_error "--zip is required"
  usage >&2
  exit 1
fi

if [[ ! -f "${zip_file}" ]]; then
  log_error "Deployment zip not found: ${zip_file}"
  exit 1
fi

log_info "Starting local deploy"
log_info "ZIP file: ${zip_file}"
log_info "Target dir (raw): ${target_dir}"
log_info "Overwrite mode: ${overwrite_mode}"
log_info "Skip install: ${skip_install}"

if [[ "${target_dir}" == ~* ]]; then
  target_dir="${HOME}${target_dir#\~}"
fi

target_dir="$(cd "$(dirname "${target_dir}")" && pwd)/$(basename "${target_dir}")"
log_info "Target dir (resolved): ${target_dir}"

require_command unzip
require_command cmp

tmp_parent="$(dirname "${target_dir}")"
mkdir -p "${tmp_parent}"
tmp_dir="$(mktemp -d "${tmp_parent}/.deploy_yaha_tmp.XXXXXX")"
log_info "Temporary extraction dir: ${tmp_dir}"
trap cleanup_tmp_dir EXIT

log_info "Unpacking deployment ZIP"
unzip -q "${zip_file}" -d "${tmp_dir}"
log_info "ZIP unpack completed"

source_root=""
if [[ -d "${tmp_dir}/yaha" ]]; then
  source_root="${tmp_dir}/yaha"
else
  first_child_count="$(find "${tmp_dir}" -mindepth 1 -maxdepth 1 -type d | wc -l | tr -d ' ')"
  if [[ "${first_child_count}" == "1" ]]; then
    source_root="$(find "${tmp_dir}" -mindepth 1 -maxdepth 1 -type d | head -n 1)"
  else
    source_root="${tmp_dir}"
  fi
fi

if [[ ! -d "${source_root}" ]]; then
  log_error "Could not determine unpacked deployment directory."
  exit 1
fi

log_info "Using source root: ${source_root}"

mkdir -p "${target_dir}"
log_info "Ensuring target directory tree"

dir_list_file="${tmp_dir}/.dir_list"
find "${source_root}" -type d | sort > "${dir_list_file}"

while IFS= read -r dir_path; do
  rel_dir="${dir_path#${source_root}/}"
  if [[ "${dir_path}" == "${source_root}" ]]; then
    continue
  fi
  mkdir -p "${target_dir}/${rel_dir}"
done < "${dir_list_file}"

copied=0
skipped_identical=0
skipped_prompt=0
journald_changed=0
nginx_changed=0

declare -A changed_components=()

log_info "Starting file copy phase"

file_list_file="${tmp_dir}/.file_list"
find "${source_root}" -type f | sort > "${file_list_file}"

while IFS= read -r src_file; do
  rel_path="${src_file#${source_root}/}"
  dst_file="${target_dir}/${rel_path}"

  src_hash="$(calc_sha256 "${src_file}")"
  dst_hash=""
  if [[ -f "${dst_file}" ]]; then
    dst_hash="$(calc_sha256 "${dst_file}")"
  fi

  if [[ -n "${dst_hash}" && "${src_hash}" == "${dst_hash}" ]]; then
    skipped_identical=$((skipped_identical + 1))
    log_info "SKIP identical ${rel_path}"
    continue
  fi

  if [[ -f "${dst_file}" && "${rel_path}" == *.ini ]]; then
    if [[ "${overwrite_mode}" == "none" ]]; then
      skipped_prompt=$((skipped_prompt + 1))
      log_info "SKIP protected-config ${rel_path}"
      continue
    fi

    if [[ "${overwrite_mode}" == "ask" ]]; then
      decision="$(prompt_overwrite "${rel_path}")"
      if [[ "${decision}" == "n" ]]; then
        skipped_prompt=$((skipped_prompt + 1))
        log_info "SKIP protected-config ${rel_path}"
        continue
      fi
      if [[ "${decision}" == "a" ]]; then
        overwrite_mode="all"
      fi
      if [[ "${decision}" == "s" ]]; then
        overwrite_mode="none"
        skipped_prompt=$((skipped_prompt + 1))
        log_info "SKIP protected-config ${rel_path}"
        continue
      fi
    fi
  fi

  mkdir -p "$(dirname "${dst_file}")"
  tmp_target="${dst_file}.new.$$"
  cp "${src_file}" "${tmp_target}"
  if [[ -x "${src_file}" ]]; then
    chmod +x "${tmp_target}"
  fi
  mv "${tmp_target}" "${dst_file}"

  copied=$((copied + 1))
  log_info "COPY ${rel_path}"

  first_segment="${rel_path%%/*}"
  if is_component "${first_segment}"; then
    changed_components["${first_segment}"]=1
  fi

  if [[ "${rel_path}" == journald/* ]]; then
    journald_changed=1
  fi

  if [[ "${rel_path}" == "nginx/controlapp.conf" ]]; then
    nginx_changed=1
  fi

done < "${file_list_file}"

log_info "DEPLOY done copied=${copied} skipped_identical=${skipped_identical} skipped_prompt=${skipped_prompt}"

if [[ ${skip_install} -eq 1 ]]; then
  log_info "Skipping install/restart actions by request (--skip-install)."
  exit 0
fi

sudo_cmd=""
if [[ ${EUID} -ne 0 ]]; then
  sudo_cmd="sudo"
fi
log_info "Install phase starts (sudo command: ${sudo_cmd:-none})"

if [[ ${journald_changed} -eq 1 ]]; then
  log_info "Applying journald namespace config updates"
  apply_journald_namespace_configs "${target_dir}" "${sudo_cmd}"
else
  log_info "Journald files unchanged; no journald restart needed."
fi

if [[ ${nginx_changed} -eq 1 ]]; then
  log_info "Applying nginx config updates"
  install_nginx_config "${target_dir}" "${sudo_cmd}"
else
  log_info "Nginx config unchanged; no nginx reload needed."
fi

for component in broker filestore msgstore automation valueservice brokerconnector httpmqttinterface remoteservice; do
  if [[ -n "${changed_components[${component}]:-}" ]]; then
    installer="${target_dir}/${component}/install.sh"
    if [[ ! -x "${installer}" ]]; then
      log_error "Missing installer for changed component: ${installer}"
      exit 1
    fi
    log_info "Installing changed component: ${component}"
    bash "${installer}"
  else
    log_info "Component unchanged: ${component}"
  fi
done

log_info "Local deployment apply completed."
