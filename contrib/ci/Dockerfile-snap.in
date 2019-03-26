FROM ubuntu:xenial

RUN apt-get update && \
  apt-get dist-upgrade --yes && \
  apt-get install --yes \
  curl sudo jq squashfs-tools && \
  curl -L $(curl -H 'X-Ubuntu-Series: 16' 'https://api.snapcraft.io/api/v1/snaps/details/core' | jq '.download_url' -r) --output core.snap && \
  mkdir -p /snap/core && unsquashfs -d /snap/core/current core.snap && rm core.snap && \
  curl -L $(curl -H 'X-Ubuntu-Series: 16' 'https://api.snapcraft.io/api/v1/snaps/details/snapcraft?channel=edge' | jq '.download_url' -r) --output snapcraft.snap && \
  mkdir -p /snap/snapcraft && unsquashfs -d /snap/snapcraft/current snapcraft.snap && rm snapcraft.snap && \
  apt remove --yes --purge curl jq squashfs-tools && \
  apt-get autoclean --yes && \
  apt-get clean --yes

COPY contrib/ci/snapcraft-wrapper /snap/bin/snapcraft
ENV PATH=/snap/bin:$PATH

LABEL maintainer="Mario Limonciello <mario.limonciello@dell.com>"
RUN apt-get update && apt-get install -y \
	curl \
	git \
	jq \
	openssh-client \
	wget
WORKDIR /root/project
