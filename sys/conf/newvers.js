// $NetBSD: newvers.js,v 1.0 2023/07/22 18:24:09 raweden Exp $


/**
 * A basic tool ontop of newvers.sh which extracts information from the git repository
 * and builds a string of information which is then passed to newvers.sh which is modified
 * to print to stdout rather than vers.c, which allows for comparing output vs. file content
 * before write, which does enables a better flow for the compiler.
 */


const fs = require("node:fs");
const constants = fs.constants;
const path = require("node:path");
const child_process = require('node:child_process');

let tmpcwd = process.cwd();
console.log("current dir: '%s'", tmpcwd);

// finds the nearest directory with a .git folder 
function has_dot_git(dirpath) {
    let ok = true;
    let gitpath = path.join(dirpath, './.git');
    try {
        fs.accessSync(gitpath, constants.R_OK);
    } catch (err) {
        ok = false;
    }

    if (ok === true) {
        return dirpath;
    }

    console.log("path '%s' ok = %s", gitpath, ok);

    let parts = dirpath.split('/');
    parts.pop();
    let pdir = path.dirname(dirpath);
    if (pdir != '/') {
        return has_dot_git(pdir);
    }

    return undefined;
}

let git_dirpath = has_dot_git(process.cwd());
console.log("git-dirpath: %s", git_dirpath);

if (!git_dirpath) {
    console.error("not a git-repo");
    process.exit(1);
}

function getRepositoryInfo(dirpath) {
    let originUrl, upstreamUrl;
    let remotes = child_process.execSync('git remote -v', {cwd: dirpath, encoding: 'utf8'});
    remotes = remotes.trim().split('\n');
    let len = remotes.length;
    for (let i = 0; i < len; i++) {
        let name, role, url, remote = remotes[i].split('\t');
        name = remote[0];
        url = remote[1];
        if (url.endsWith('\x20(fetch)')) {
            url = url.substring(0, url.length - 8);
            role = "fetch";
        } else if (url.endsWith('\x20(push)')) {
            url = url.substring(0, url.length - 7);
            role = "push";
        }

        if (name == "origin" && role == "push") {
            originUrl = url;
        } else if (name == "upstream" && role == "fetch") {
            upstreamUrl = url;
        }

        console.log("name = %s url = %s role = %s", name, url, role);
    }
    

    // git rev-parse --abbrev-ref HEAD

    let commitId = child_process.execSync('git rev-parse --verify --short HEAD', {cwd: dirpath, encoding: 'utf8'});
    commitId = commitId.trim();
    console.log("commit-hash: '%s'", commitId);

    let is_shallow_repository = child_process.execSync('git rev-parse --is-shallow-repository', {cwd: dirpath, encoding: 'utf8'});
    is_shallow_repository = is_shallow_repository.trim() !== "false";
    console.log("is_shallow_repository: %s", is_shallow_repository);

    let dirty = child_process.execSync('git diff --quiet || echo \'dirty\'', {cwd: dirpath, encoding: 'utf8'});
    dirty = dirty.trim()
    console.log("dirty: '%s'", dirty);

    let branch = child_process.execSync('git rev-parse --abbrev-ref HEAD', {cwd: dirpath, encoding: 'utf8'});
    branch = branch.trim();
    console.log("branch: '%s'", branch.trim());

    let count = child_process.execSync('git rev-list --first-parent --count HEAD', {cwd: dirpath, encoding: 'utf8'});
    count = count.trim();
    console.log("branch: '%s'", count.trim());
    

    let worktree = child_process.execSync('git worktree list', {cwd: dirpath, encoding: 'utf8'});
    console.log("worktree: '%s'", worktree.trim());

    let git = branch;
    if (count) {
        git += "-" + count;
    }
    git += "-" + commitId;
    if (dirty) {
        git += "-" + dirty;
    }
    if (originUrl) {
        git += "\x20(" + originUrl + ')';
    }
    //if (upstreamUrl) {
    //    git += "\x20(upstream: " + upstreamUrl + ')';
    //}

    git += "\n";

    return git;
}


let repo = getRepositoryInfo(git_dirpath);
console.log(repo);

let machine = "wasm";
let confpath = "./"
let newvers_path = path.join(git_dirpath, './sys/conf');
newvers_path = path.relative(process.cwd(), newvers_path);
newvers_path = path.join(newvers_path, 'newvers.sh');
console.log("newvers_path: %s", newvers_path);

let cmd = `sh ${newvers_path} -m ${machine} -n -R`
let sh_env = Object.assign({}, process.env);
sh_env.BUILDINFO = repo;

let src = child_process.execSync(cmd, {cwd: process.cwd(), env: sh_env, encoding: 'utf8'});
console.log("src: '%s'", src);

let cmp = fs.readFileSync("./vers.c", {encoding: 'utf8'});
if (src != cmp) {
    fs.writeFileSync("./vers.c", src, {encoding: 'utf8'});
    console.log("did write");
} else {
    console.log("no update");
}