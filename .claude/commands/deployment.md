Deployment skill

Goal
Make deployment package from source.
Copy package to target host.
Install services and nginx config.

Rules
Use only source nginx file at cmake/nginx/controlapp.conf.
Never use second nginx source file.
If source file missing then stop.
If source and deployment nginx differ then stop.

Create package
Run from repository root.
Command:
python3 cmake/create_yaha_deployment.py --build --preset armv7-zig-release

Expected result
Package path deployment/yaha exists.
File deployment/yaha/nginx/controlapp.conf exists.
Script prints:
Nginx source used: .../cmake/nginx/controlapp.conf
Nginx source->deployment verification: OK

Deploy package
Run from repository root.
Command:
python3 cmake/deploy_yaha_scp.py --remote-host pi@yaha2 --remote-dir mqtt --no-overwrite-ini --install-root

What install-root does
Runs remote deployment install.sh.
install.sh installs all service units.
install.sh installs nginx controlapp config from deployment/yaha/nginx/controlapp.conf.
install.sh tests nginx config and reloads nginx only when config changed.

Troubleshoot drift
If deployment config not expected:
1. Delete deployment/yaha directory.
2. Re-run create_yaha_deployment.py.
3. Check printed nginx source path.
4. Check verification line is OK.
5. Deploy again.

Validation quick
On target host run:
sudo nginx -t
curl -i -X OPTIONS http://127.0.0.1/kvstore/test
curl -i http://127.0.0.1/kvstore/test
