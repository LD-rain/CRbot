const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const VERSION = /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/;
const SHA = /^[0-9a-f]{40}$/;
const PLATFORMS = ['win64', 'macos-arm64', 'macos-x86_64'];

function packageNames(version) {
  return PLATFORMS.map(platform => `CRBot-${version}-${platform}${platform === 'win64' ? '-setup.exe' : '.dmg'}`);
}

function versionInfo(version, build, channel) {
  if (!VERSION.test(version)) throw new Error('VERSION must contain X.X.X');
  if (!/^[1-9]\d*$/.test(String(build))) throw new Error('Build number must be a positive integer');
  if (!['prerelease', 'stable'].includes(channel)) throw new Error('Unknown release channel');
  return {version, build_number: String(build), channel,
    display_version: channel === 'stable' ? version : `${version}(${build})`};
}

function mergedRoute(pr, branch, sha, repository) {
  if (!pr.merged_at || pr.merge_commit_sha !== sha || pr.base?.ref !== branch ||
      pr.base?.repo?.full_name !== repository) return null;
  if (branch === 'dev') return 'prerelease';
  if (branch === 'main' && pr.head?.ref === 'dev' && pr.head?.repo?.full_name === repository) return 'stable';
  return null;
}

async function prepare({github, context, core, versionPath = 'VERSION', env = process.env,
                        sleep = ms => new Promise(resolve => setTimeout(resolve, ms))}) {
  const repository = `${context.repo.owner}/${context.repo.repo}`;
  let channel = 'prerelease';
  let shouldBuild = false;
  let shouldPublish = false;
  if (context.eventName === 'pull_request') {
    const pr = context.payload.pull_request;
    shouldBuild = pr.base.ref === 'dev' ||
      (pr.base.ref === 'main' && pr.head.ref === 'dev' && pr.head.repo?.full_name === repository);
    if (!shouldBuild) throw new Error('Only feature -> dev and same-repository dev -> main PRs are supported');
  } else if (context.eventName === 'push' && !context.payload.deleted) {
    const branch = context.ref.replace(/^refs\/heads\//, '');
    if (['dev', 'main'].includes(branch)) {
      // A push alone is not authorization to publish: verify the merged PR and
      // its exact merge SHA (also works for squash/rebase). Retry API indexing.
      for (let attempt = 0; attempt < 6; attempt++) {
        const prs = await github.paginate(github.rest.repos.listPullRequestsAssociatedWithCommit,
          {...context.repo, commit_sha: context.sha, per_page: 100});
        const route = prs.map(pr => mergedRoute(pr, branch, context.sha, repository)).find(Boolean);
        if (route) {
          channel = route;
          shouldBuild = shouldPublish = true;
          break;
        }
        if (attempt < 5) await sleep(5000);
      }
    }
  }
  if (!SHA.test(context.sha)) throw new Error('Invalid source commit');
  const info = versionInfo(fs.readFileSync(versionPath, 'utf8').trim(), env.GITHUB_RUN_NUMBER, channel);
  const outputs = {...info, build: String(shouldBuild), publish: String(shouldPublish), source_sha: context.sha};
  for (const [key, value] of Object.entries(outputs)) core.setOutput(key, value);
  core.info(shouldBuild ? `${shouldPublish ? 'CD' : 'CI'}: ${info.display_version} (${channel})`
    : 'No matching merged PR: skipping build and release');
  return outputs;
}

async function publish({github, context, core, directory, env = process.env}) {
  const version = env.CRBOT_RELEASE_VERSION;
  const build = env.CRBOT_RELEASE_BUILD;
  const channel = env.CRBOT_RELEASE_CHANNEL;
  const sha = env.CRBOT_RELEASE_SHA;
  const semantic = channel === 'prerelease' ? version?.replace(/\(\d+\)$/, '') : version;
  const expected = versionInfo(semantic, build, channel);
  if (version !== expected.display_version || !SHA.test(sha)) throw new Error('Invalid release metadata');
  if (context.eventName !== 'push' || context.sha !== sha) throw new Error('Releases require the validated push commit');
  if ((channel === 'stable' && context.ref !== 'refs/heads/main') ||
      (channel === 'prerelease' && context.ref !== 'refs/heads/dev')) throw new Error('Release branch mismatch');

  const packages = packageNames(version);
  const names = packages.flatMap(name => [name, `${name}.sha256`]).sort();
  const actual = fs.readdirSync(directory).sort();
  if (JSON.stringify(actual) !== JSON.stringify(names)) throw new Error('All three platform packages and checksums are required');
  // Validate every byte before creating or modifying any release.
  for (const name of packages) {
    const digest = crypto.createHash('sha256').update(fs.readFileSync(path.join(directory, name))).digest('hex');
    if (fs.readFileSync(path.join(directory, `${name}.sha256`), 'utf8').trim() !== `${digest}  ${name}`)
      throw new Error(`Checksum mismatch: ${name}`);
  }

  const tag = `v${version}`;
  const marker = `<!-- crbot-build:${env.GITHUB_RUN_ID}:${build}:${sha} -->`;
  const releases = await github.paginate(github.rest.repos.listReleases, {...context.repo, per_page: 100});
  let release = releases.find(item => item.tag_name === tag);
  // Never overwrite a release/tag from another build. Re-runs may finish only
  // their own draft, or return successfully if that release is already public.
  if (release && (!release.body?.includes(marker) || release.target_commitish !== sha))
    throw new Error(`${tag} already exists for another build. Update VERSION before a new stable release.`);
  try {
    let ref = (await github.rest.git.getRef({...context.repo, ref: `tags/${tag}`})).data.object;
    while (ref.type === 'tag') ref = (await github.rest.git.getTag({...context.repo, tag_sha: ref.sha})).data.object;
    if (ref.type !== 'commit' || ref.sha !== sha) throw new Error(`${tag} points to another commit`);
  } catch (error) { if (error.status !== 404) throw error; }
  if (release && !release.draft) {
    core.info(`${version} was already published by this build`);
    return release;
  }
  if (!release) {
    release = (await github.rest.repos.createRelease({...context.repo, tag_name: tag,
      target_commitish: sha, name: version, draft: true, prerelease: channel === 'prerelease',
      body: `${marker}\n\nVersion: ${version}\n\nBuild: ${build}\n\nCommit: ${sha}\n\n` +
        `Windows x64 setup executable; macOS Apple Silicon and Intel DMG installers. SHA256 checksums are included.\n\n` +
        `[Build logs](${context.serverUrl}/${context.repo.owner}/${context.repo.repo}/actions/runs/${env.GITHUB_RUN_ID})`
    })).data;
  }
  const existing = await github.paginate(github.rest.repos.listReleaseAssets,
    {...context.repo, release_id: release.id, per_page: 100});
  for (const asset of existing) {
    if (!names.includes(asset.label || asset.name)) throw new Error(`Unexpected draft asset: ${asset.name}`);
    await github.rest.repos.deleteReleaseAsset({...context.repo, asset_id: asset.id});
  }
  for (const name of packages) {
    const data = fs.readFileSync(path.join(directory, name));
    const contentType = name.endsWith('.exe') ? 'application/vnd.microsoft.portable-executable' : 'application/x-apple-diskimage';
    const uploaded = (await github.rest.repos.uploadReleaseAsset({...context.repo,
      release_id: release.id, name, label: name, data,
      headers: {'content-type': contentType, 'content-length': data.length}})).data;
    // GitHub can normalize special characters in download filenames. Keep the
    // requested display label and make the checksum refer to the returned name.
    if (!uploaded.name || /[\r\n/\\]/.test(uploaded.name)) throw new Error('Invalid uploaded filename');
    const digest = crypto.createHash('sha256').update(data).digest('hex');
    const checksum = Buffer.from(`${digest}  ${uploaded.name}\n`);
    await github.rest.repos.uploadReleaseAsset({...context.repo, release_id: release.id,
      name: `${name}.sha256`, label: `${name}.sha256`, data: checksum,
      headers: {'content-type': 'text/plain', 'content-length': checksum.length}});
  }
  release = (await github.rest.repos.updateRelease({...context.repo, release_id: release.id,
    draft: false, prerelease: channel === 'prerelease', make_latest: channel === 'stable' ? 'true' : 'false'})).data;
  core.info(`Published ${version}: ${release.html_url}`);
  return release;
}

module.exports = {versionInfo, packageNames, mergedRoute, prepare, publish};
