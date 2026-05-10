#!/usr/bin/env python3
from __future__ import annotations

"""Create a copy-ready YAHA deployment directory with install scripts."""

import argparse
import shutil
import stat
import subprocess
import sys
from pathlib import Path


def _detect_project_root() -> Path:
    # Keep the script path lexical to avoid resolving symlinks into transient paths.
    script_path = Path(__file__).absolute()
    primary = script_path.parent.parent
    if (primary / "CMakePresets.json").exists():
        return primary

    # Fallback for execution from within the repository (e.g. cwd is project root or cmake/).
    cwd = Path.cwd().absolute()
    fallback = cwd.parent if cwd.name == "cmake" else cwd
    if (fallback / "CMakePresets.json").exists():
        return fallback

    raise RuntimeError(
        "Could not detect project root containing CMakePresets.json. "
        f"Checked: {primary} and {fallback}"
    )


PROJECT_ROOT = _detect_project_root()
DEFAULT_PRESET = "armv7-zig-release"
DEFAULT_OUTPUT_DIR = PROJECT_ROOT / "deployment" / "yaha"
INI_DIR = PROJECT_ROOT / "cmake" / "ini"
NGINX_CONTROLAPP_SOURCE = PROJECT_ROOT / "cmake" / "nginx" / "controlapp.conf"

SERVICE_COMPONENTS = (
    {
        "name": "broker",
        "binary": "yahabroker",
        "ini": "broker.ini",
        "service": "yahabroker.service",
        "description": "Yaha MQTT Broker",
        "exec": "__INSTALL_ROOT__/broker/yahabroker -c __INSTALL_ROOT__/broker/broker.ini",
    },
    {
        "name": "filestore",
        "binary": "yahafilestoreclient",
        "ini": "filestore.ini",
        "service": "yahafilestoreclient.service",
        "description": "Yaha File Store Client",
        "exec": (
            "__INSTALL_ROOT__/filestore/yahafilestoreclient "
            "__INSTALL_ROOT__/filestore/filestore.ini"
        ),
    },
    {
        "name": "msgstore",
        "binary": "yahamsgstoreclient",
        "ini": "msgstore.ini",
        "service": "yahamsgstoreclient.service",
        "description": "Yaha Message Store Client",
        "exec": (
            "__INSTALL_ROOT__/msgstore/yahamsgstoreclient "
            "__INSTALL_ROOT__/msgstore/msgstore.ini"
        ),
    },
    {
        "name": "automation",
        "binary": "yahaautomationclient",
        "ini": "automation.ini",
        "service": "yahaautomationclient.service",
        "description": "Yaha Automation Client",
        "exec": (
            "__INSTALL_ROOT__/automation/yahaautomationclient "
            "__INSTALL_ROOT__/automation/automation.ini"
        ),
    },
    {
        "name": "valueservice",
        "binary": "yahavalueserviceclient",
        "ini": "valueservice.ini",
        "service": "yahavalueserviceclient.service",
        "description": "Yaha Value Service Client",
        "exec": (
            "__INSTALL_ROOT__/valueservice/yahavalueserviceclient "
            "__INSTALL_ROOT__/valueservice/valueservice.ini"
        ),
    },
    {
        "name": "brokerconnector",
        "binary": "yahabrokerconnectorclient",
        "ini": "brokerconnector.ini",
        "service": "yahabrokerconnectorclient.service",
        "description": "Yaha Broker Connector Client",
        "exec": (
            "__INSTALL_ROOT__/brokerconnector/yahabrokerconnectorclient "
            "__INSTALL_ROOT__/brokerconnector/brokerconnector.ini"
        ),
    },
    {
        "name": "httpmqttinterface",
        "binary": "yahahttpmqttinterfaceclient",
        "ini": "httpmqttinterface.ini",
        "service": "yahahttpmqttinterfaceclient.service",
        "description": "Yaha HTTP MQTT Interface Client",
        "exec": (
            "__INSTALL_ROOT__/httpmqttinterface/yahahttpmqttinterfaceclient "
            "__INSTALL_ROOT__/httpmqttinterface/httpmqttinterface.ini"
        ),
    },
)

ROOT_TOOLS = (
    {
        "name": "svc",
        "source": PROJECT_ROOT / "src" / "svc" / "svc",
        "target_name": "svc",
    },
)


def run_or_fail(command: list[str], cwd: Path) -> None:
    completed = subprocess.run(command, cwd=str(cwd), check=False)
    if completed.returncode != 0:
        raise RuntimeError(
            f"Command failed ({completed.returncode}): {' '.join(command)}"
        )


def set_executable(path: Path) -> None:
    mode = path.stat().st_mode
    path.chmod(mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def ensure_ini_templates() -> None:
    missing: list[Path] = []
    for component in SERVICE_COMPONENTS:
        ini_path = INI_DIR / component["ini"]
        if not ini_path.exists():
            missing.append(ini_path)

    for tool_component in ROOT_TOOLS:
        if not tool_component["source"].exists():
            missing.append(tool_component["source"])

    if not NGINX_CONTROLAPP_SOURCE.exists():
        missing.append(NGINX_CONTROLAPP_SOURCE)

    if missing:
        missing_lines = "\n".join(str(path) for path in missing)
        raise RuntimeError(f"Missing tool files:\n{missing_lines}")


def render_service_file(*, description: str, component_name: str, exec_start: str) -> str:
    return "\n".join(
        [
            "[Unit]",
            f"Description={description}",
            "After=network-online.target",
            "Wants=network-online.target",
            "",
            "[Service]",
            "Type=simple",
            "User=__SERVICE_USER__",
            "Group=__SERVICE_USER__",
            f"WorkingDirectory=__INSTALL_ROOT__/{component_name}",
            f"ExecStart={exec_start}",
            "Restart=always",
            "RestartSec=3",
            "StandardOutput=journal",
            "StandardError=journal",
            "",
            "[Install]",
            "WantedBy=multi-user.target",
            "",
        ]
    )


def render_component_install_script(*, service_name: str) -> str:
    return "\n".join(
        [
            "#!/usr/bin/env bash",
            "set -euo pipefail",
            "",
            "SCRIPT_DIR=\"$(cd \"$(dirname \"${BASH_SOURCE[0]}\")\" && pwd)\"",
            "INSTALL_ROOT=\"$(cd \"${SCRIPT_DIR}/..\" && pwd)\"",
            f"SERVICE_TEMPLATE=\"${{SCRIPT_DIR}}/{service_name}\"",
            f"SERVICE_TARGET=\"/etc/systemd/system/{service_name}\"",
            "SERVICE_USER=\"${YAHA_SERVICE_USER:-$(id -un)}\"",
            "",
            "if [[ ! -f \"${SERVICE_TEMPLATE}\" ]]; then",
            "  echo \"Missing service template: ${SERVICE_TEMPLATE}\" >&2",
            "  exit 1",
            "fi",
            "",
            "mkdir -p \"${SCRIPT_DIR}/data\"",
            "",
            "tmp_service=\"$(mktemp)\"",
            "trap 'rm -f \"${tmp_service}\"' EXIT",
            "sed -e \"s|__INSTALL_ROOT__|${INSTALL_ROOT}|g\" \\",
            "    -e \"s|__SERVICE_USER__|${SERVICE_USER}|g\" \\",
            "    \"${SERVICE_TEMPLATE}\" > \"${tmp_service}\"",
            "",
            "if [[ ${EUID} -eq 0 ]]; then",
            "  cp \"${tmp_service}\" \"${SERVICE_TARGET}\"",
            "  chown -R \"${SERVICE_USER}:${SERVICE_USER}\" \"${SCRIPT_DIR}/data\" || true",
            "  systemctl daemon-reload",
            f"  systemctl enable {service_name}",
            f"  systemctl restart {service_name}",
            "else",
            "  sudo cp \"${tmp_service}\" \"${SERVICE_TARGET}\"",
            "  sudo chown -R \"${SERVICE_USER}:${SERVICE_USER}\" \"${SCRIPT_DIR}/data\" || true",
            "  sudo systemctl daemon-reload",
            f"  sudo systemctl enable {service_name}",
            f"  sudo systemctl restart {service_name}",
            "fi",
            "",
            f"echo \"Installed service {service_name} for ${'{'}SERVICE_USER{'}'}\"",
        ]
    )


def render_root_install_script() -> str:
    install_order = [
        "broker",
        "filestore",
        "msgstore",
        "automation",
        "valueservice",
        "brokerconnector",
        "httpmqttinterface",
    ]
    return "\n".join(
        [
            "#!/usr/bin/env bash",
            "set -euo pipefail",
            "",
            "SCRIPT_DIR=\"$(cd \"$(dirname \"${BASH_SOURCE[0]}\")\" && pwd)\"",
            "SUDO=\"\"",
            "if [[ ${EUID} -ne 0 ]]; then",
            "  SUDO=\"sudo\"",
            "fi",
            "",
            "install_nginx_config() {",
            "  local src_conf=\"${SCRIPT_DIR}/nginx/controlapp.conf\"",
            "  local target_conf=\"/etc/nginx/sites-available/controlapp\"",
            "  local enabled_link=\"/etc/nginx/sites-enabled/controlapp\"",
            "  local obsolete_link=\"/etc/nginx/sites-enabled/controlapp.conf\"",
            "",
            "  if [[ ! -f \"${src_conf}\" ]]; then",
            "    echo \"Skipping nginx setup (missing ${src_conf}).\"",
            "    return 0",
            "  fi",
            "",
            "  ${SUDO} mkdir -p /etc/nginx/sites-available /etc/nginx/sites-enabled",
            "  ${SUDO} rm -f \"${obsolete_link}\"",
            "",
            "  local changed=0",
            "  local had_backup=0",
            "  local backup_conf=\"\"",
            "  if [[ ! -f \"${target_conf}\" ]] || ! cmp -s \"${src_conf}\" \"${target_conf}\"; then",
            "    changed=1",
            "    if [[ -f \"${target_conf}\" ]]; then",
            "      backup_conf=\"${target_conf}.bak.$(date +%Y%m%d%H%M%S)\"",
            "      ${SUDO} cp \"${target_conf}\" \"${backup_conf}\"",
            "      had_backup=1",
            "    fi",
            "    ${SUDO} cp \"${src_conf}\" \"${target_conf}\"",
            "  fi",
            "",
            "  ${SUDO} ln -sfn \"${target_conf}\" \"${enabled_link}\"",
            "",
            "  if ! ${SUDO} nginx -t; then",
            "    echo \"nginx -t failed, rolling back controlapp.conf\" >&2",
            "    if [[ ${had_backup} -eq 1 ]]; then",
            "      ${SUDO} cp \"${backup_conf}\" \"${target_conf}\"",
            "    elif [[ ${changed} -eq 1 ]]; then",
            "      ${SUDO} rm -f \"${target_conf}\"",
            "      ${SUDO} rm -f \"${enabled_link}\"",
            "    fi",
            "    ${SUDO} nginx -t || true",
            "    return 1",
            "  fi",
            "",
            "  if [[ ${changed} -eq 1 ]]; then",
            "    ${SUDO} systemctl reload nginx",
            "    echo \"Installed nginx controlapp.conf and reloaded nginx.\"",
            "  else",
            "    echo \"nginx controlapp.conf unchanged; no reload needed.\"",
            "  fi",
            "}",
            "",
            "for component in " + " ".join(install_order) + "; do",
            "  installer=\"${SCRIPT_DIR}/${component}/install.sh\"",
            "  if [[ ! -x \"${installer}\" ]]; then",
            "    echo \"Missing installer: ${installer}\" >&2",
            "    exit 1",
            "  fi",
            "  echo \"Installing ${component}...\"",
            "  bash \"${installer}\"",
            "done",
            "",
            "install_nginx_config",
            "",
            "echo \"YAHA installation completed.\"",
        ]
    )
def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Create a copy-ready YAHA deployment directory with service "
            "components and helper tools."
        )
    )
    parser.add_argument(
        "--preset",
        default=DEFAULT_PRESET,
        help="CMake preset used when --build is enabled",
    )
    parser.add_argument(
        "--build-dir",
        default="",
        help="Directory containing built binaries (default: build/<preset>)",
    )
    parser.add_argument(
        "--output-dir",
        default=str(DEFAULT_OUTPUT_DIR),
        help="Deployment output directory",
    )
    parser.add_argument(
        "--build",
        action="store_true",
        help="Run cmake --build before packaging",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    try:
        ensure_ini_templates()

        build_dir = (
            Path(args.build_dir).expanduser()
            if args.build_dir
            else PROJECT_ROOT / "build" / args.preset
        )
        output_dir = Path(args.output_dir).expanduser()
        nginx_controlapp_content = NGINX_CONTROLAPP_SOURCE.read_text(encoding="utf-8")

        if args.build:
            run_or_fail(
                [
                    "cmake",
                    "--build",
                    "--preset",
                    args.preset,
                ],
                PROJECT_ROOT,
            )

        if output_dir.exists():
            shutil.rmtree(output_dir)
        output_dir.mkdir(parents=True, exist_ok=True)

        nginx_dir = output_dir / "nginx"
        nginx_dir.mkdir(parents=True, exist_ok=True)
        (nginx_dir / "controlapp.conf").write_text(nginx_controlapp_content, encoding="utf-8")

        for component in SERVICE_COMPONENTS:
            component_dir = output_dir / component["name"]
            component_dir.mkdir(parents=True, exist_ok=True)

            source_binary = build_dir / component["binary"]
            if not source_binary.exists():
                raise RuntimeError(
                    f"Missing binary '{component['binary']}' in {build_dir}. "
                    "Build first or pass --build-dir."
                )

            target_binary = component_dir / component["binary"]
            shutil.copy2(source_binary, target_binary)
            set_executable(target_binary)

            ini_source = INI_DIR / component["ini"]
            shutil.copy2(ini_source, component_dir / component["ini"])

            service_content = render_service_file(
                description=component["description"],
                component_name=component["name"],
                exec_start=component["exec"],
            )
            service_path = component_dir / component["service"]
            service_path.write_text(service_content, encoding="utf-8")

            install_script_content = render_component_install_script(
                service_name=component["service"],
            )
            install_script_path = component_dir / "install.sh"
            install_script_path.write_text(install_script_content, encoding="utf-8")
            set_executable(install_script_path)

        for tool_component in ROOT_TOOLS:
            source_tool = tool_component["source"]
            target_tool = output_dir / tool_component["target_name"]
            shutil.copy2(source_tool, target_tool)
            set_executable(target_tool)

        root_install = output_dir / "install.sh"
        root_install.write_text(render_root_install_script(), encoding="utf-8")
        set_executable(root_install)

        print(f"Deployment package created: {output_dir}")
        print("Copy this directory 1:1 to the target system and run ./install.sh there.")
        return 0
    except Exception as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())