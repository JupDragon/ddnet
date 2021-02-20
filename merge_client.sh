git fetch --all
git reset --hard upstream/master 

#enable soon
if [[ ! -z ${1+x} && "${1}" == "1" ]]; then
	git merge origin/client_cmake
fi
git merge origin/client_quake
git merge origin/client_text_color_parser
git merge origin/client_modern_gl
git merge origin/pr_fng_scoreboard
git merge origin/pr_libpng
git merge origin/pr_cache_serverlist
git merge origin/pr_fetch_two_masters
