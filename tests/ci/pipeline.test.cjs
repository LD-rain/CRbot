const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const {versionInfo, packageNames, mergedRoute, prepare, publish} = require('../../.github/scripts/pipeline.cjs');

const sha = 'a'.repeat(40);
const repository = 'example/CRbot';
const pr = (base = 'dev', head = 'feature') => ({
  base: {ref: base, repo: {full_name: repository}},
  head: {ref: head, repo: {full_name: repository}},
  merge_commit_sha: sha, merged_at: '2026-10-26T00:00:00Z'
});
const context = (eventName = 'push', branch = 'dev') => ({
  eventName, ref: `refs/heads/${branch}`, sha, repo: {owner: 'example', repo: 'CRbot'},
  serverUrl: 'https://github.com', payload: {}
});
const core = {setOutput() {}, info() {}};

test('pre-release and stable naming; malformed metadata is rejected', () => {
  assert.equal(versionInfo('1.1.0', 3, 'prerelease').display_version, '1.1.0(3)');
  assert.equal(versionInfo('1.1.1', 4, 'stable').display_version, '1.1.1');
  for (const version of ['v1.1.0', '01.1.0', '1.1', '1.1.0;exit'])
    assert.throws(() => versionInfo(version, 3, 'prerelease'));
  assert.throws(() => versionInfo('1.1.0', 0, 'stable'));
  assert.deepEqual(packageNames('1.1.0(3)'), [
    'CRBot-1.1.0(3)-win64-setup.exe',
    'CRBot-1.1.0(3)-macos-arm64.dmg',
    'CRBot-1.1.0(3)-macos-x86_64.dmg'
  ]);
});

test('only the exact merged PR and permitted direction can publish', () => {
  assert.equal(mergedRoute(pr(), 'dev', sha, repository), 'prerelease');
  assert.equal(mergedRoute(pr('main', 'dev'), 'main', sha, repository), 'stable');
  assert.equal(mergedRoute(pr('main', 'feature'), 'main', sha, repository), null);
  assert.equal(mergedRoute({...pr(), merged_at: null}, 'dev', sha, repository), null);
  assert.equal(mergedRoute(pr(), 'dev', 'b'.repeat(40), repository), null);
  const fork = pr('main', 'dev');
  fork.head.repo.full_name = 'someone/CRbot';
  assert.equal(mergedRoute(fork, 'main', sha, repository), null);
});

test('CI builds PR merge SHA with a shared run number, never publishes', async () => {
  const ctx = context('pull_request');
  ctx.payload.pull_request = pr();
  const outputs = await prepare({context: ctx, core, env: {GITHUB_RUN_NUMBER: '3'}});
  assert.equal(outputs.build, 'true');
  assert.equal(outputs.publish, 'false');
  assert.equal(outputs.build_number, '3');
  assert.equal(outputs.source_sha, sha);
});

test('direct push does not publish; API indexing is retried for a merged push', async () => {
  let calls = 0;
  const github = {rest: {repos: {listPullRequestsAssociatedWithCommit: 'prs'}},
    paginate: async () => { calls++; return []; }};
  const args = {context: context(), github, core, env: {GITHUB_RUN_NUMBER: '4'}, sleep: async () => {}};
  assert.equal((await prepare(args)).publish, 'false');
  assert.equal(calls, 6);
  calls = 0;
  github.paginate = async () => ++calls === 1 ? [] : [pr()];
  const outputs = await prepare(args);
  assert.equal(outputs.publish, 'true');
  assert.equal(outputs.channel, 'prerelease');
  assert.equal(calls, 2);
  args.context = context('push', 'main');
  github.paginate = async () => [pr('main', 'dev')];
  assert.equal((await prepare(args)).channel, 'stable');
});

function releaseFixture(t, channel = 'prerelease') {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'crbot-release-'));
  t.after(() => fs.rmSync(directory, {recursive: true, force: true}));
  const version = channel === 'prerelease' ? '1.1.0(3)' : '1.1.1';
  for (const platform of ['win64', 'macos-arm64', 'macos-x86_64']) {
    const name = `CRBot-${version}-${platform}${platform === 'win64' ? '-setup.exe' : '.dmg'}`;
    const data = Buffer.from('fixture bytes');
    fs.writeFileSync(path.join(directory, name), data);
    fs.writeFileSync(path.join(directory, name + '.sha256'),
      crypto.createHash('sha256').update(data).digest('hex') + '  ' + name + '\n');
  }
  const events = [];
  let releases = [];
  let assets = [];
  const github = {
    paginate: async method => method === 'releases' ? releases : assets,
    rest: {
      git: {getRef: async () => { throw Object.assign(new Error('Not found'), {status: 404}); }},
      repos: {
        listReleases: 'releases', listReleaseAssets: 'assets',
        createRelease: async options => {
          events.push(['create', options]);
          releases = [{...options, id: 10}];
          return {data: releases[0]};
        },
        deleteReleaseAsset: async options => { events.push(['delete', options]); },
        uploadReleaseAsset: async options => {
          events.push(['upload', options]);
          const asset = {id: assets.length + 1, name: options.name, label: options.label};
          assets.push(asset);
          return {data: asset};
        },
        updateRelease: async options => {
          events.push(['publish', options]);
          Object.assign(releases[0], options);
          return {data: releases[0]};
        }
      }
    }
  };
  const args = {github, context: context('push', channel === 'stable' ? 'main' : 'dev'), core, directory,
    env: {CRBOT_RELEASE_VERSION: version, CRBOT_RELEASE_BUILD: '3', CRBOT_RELEASE_CHANNEL: channel,
      CRBOT_RELEASE_SHA: sha, GITHUB_RUN_ID: '123'}};
  return {args, events, github, releases: () => releases};
}

test('publish stages all three platform packages before exposing the release; retry is idempotent', async t => {
  const f = releaseFixture(t);
  await publish(f.args);
  assert.equal(f.events[0][1].draft, true);
  assert.equal(f.events[0][1].name, '1.1.0(3)');
  assert.equal(f.events.filter(e => e[0] === 'upload').length, 6);
  assert.equal(f.events.at(-1)[0], 'publish');
  assert.equal(f.events.at(-1)[1].prerelease, true);
  for (const [, asset] of f.events.filter(e => e[0] === 'upload')) {
    assert.equal(asset.headers['content-type'], asset.name.endsWith('.exe')
      ? 'application/vnd.microsoft.portable-executable'
      : asset.name.endsWith('.dmg') ? 'application/x-apple-diskimage' : 'text/plain');
  }
  const count = f.events.length;
  await publish(f.args);
  assert.equal(f.events.length, count);
});

test('stable release hides build number, sets latest, and rejects version reuse', async t => {
  const f = releaseFixture(t, 'stable');
  await publish(f.args);
  assert.equal(f.events[0][1].name, '1.1.1');
  assert.equal(f.events[0][1].tag_name, 'v1.1.1');
  assert.equal(f.events.at(-1)[1].prerelease, false);
  assert.equal(f.events.at(-1)[1].make_latest, 'true');
  f.args.env.GITHUB_RUN_ID = '124';
  await assert.rejects(publish(f.args), /another build/);
});

test('missing or corrupted package prevents every release mutation', async t => {
  const f = releaseFixture(t);
  const file = path.join(f.args.directory, 'CRBot-1.1.0(3)-win64-setup.exe');
  fs.writeFileSync(file, 'corrupted');
  await assert.rejects(publish(f.args), /Checksum/);
  assert.equal(f.events.length, 0);
  fs.unlinkSync(file);
  await assert.rejects(publish(f.args), /All three/);
  assert.equal(f.events.length, 0);
});

test('failed upload leaves a private draft that the same build can finish', async t => {
  const f = releaseFixture(t);
  const upload = f.github.rest.repos.uploadReleaseAsset;
  let fail = true;
  f.github.rest.repos.uploadReleaseAsset = async options => {
    if (fail && options.name.endsWith('.sha256')) throw new Error('Temporary upload error');
    return upload(options);
  };
  await assert.rejects(publish(f.args), /Temporary/);
  assert.equal(f.releases()[0].draft, true);
  fail = false;
  await publish(f.args);
  assert.equal(f.events.filter(e => e[0] === 'create').length, 1);
  assert.equal(f.releases()[0].draft, false);
});

test('GitHub filename normalization preserves labels and downloadable checksum names', async t => {
  const f = releaseFixture(t);
  const upload = f.github.rest.repos.uploadReleaseAsset;
  f.github.rest.repos.uploadReleaseAsset = async options => {
    const response = await upload(options);
    response.data.name = options.name.replace(/[()]/g, '.');
    return response;
  };
  await publish(f.args);
  for (const [, asset] of f.events.filter(e => e[0] === 'upload')) {
    assert.ok(asset.label.includes('1.1.0(3)'));
    if (asset.name.endsWith('.sha256')) {
      const installer = asset.name.slice(0, -'.sha256'.length).replace(/[()]/g, '.');
      assert.ok(asset.data.toString().endsWith(`  ${installer}\n`));
    }
  }
});

test('legacy ZIP or extra attachment prevents release publication', async t => {
  const f = releaseFixture(t);
  fs.writeFileSync(path.join(f.args.directory, 'CRBot-1.1.0(3)-win64.zip'), 'legacy');
  await assert.rejects(publish(f.args), /All three/);
  assert.equal(f.events.length, 0);
});
