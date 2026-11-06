# NetGPU-ICD TODO

## Completed

- [x] Add the root CMake build with C++17, warnings, PIC and `-fno-exceptions -fno-rtti`.
- [x] Add the 56-byte wire header and explicit big-endian encode/decode.
- [x] Validate header magic, version, header length, fragment bounds and the 64 MiB message ceiling.
- [x] Add AF_UNIX `SOCK_STREAM` exact-read/exact-write transport.
- [x] Test partial reads, disconnects, oversized messages and concurrent request/reply traffic over AF_UNIX.
- [x] Add Ethernet frame encode/decode for EtherType `0x88B5` with a bounded payload.
- [x] Add a Linux AF_PACKET socket bound to a selected interface.
- [x] Add a statically linked `packet_link_smoke` utility.
- [x] Exercise AF_PACKET send/receive in both directions over a private veth pair.

## Ethernet And Wire Integration

- [ ] Add a `MessageType` enum for discovery, handshake, capabilities, requests, responses and errors.
- [ ] Reject unknown message types in the common validator.
- [ ] Add helpers to encode/decode a `WireHeader` plus fragment as one Ethernet payload.
- [ ] Validate the decoded wire header before exposing the fragment to a handler.
- [ ] Replace the smoke tool's private `NGPU` ping payload with the common wire header and message type.
- [ ] Query the selected interface MTU and derive the maximum frame payload from it.
- [ ] Reject outbound frames that exceed the selected interface MTU.
- [ ] Add tests for standard MTU, configured jumbo MTU and one-byte-over-limit frames.
- [ ] Add explicit VLAN-tag handling, or reject tagged frames with a clear diagnostic.
- [ ] Make packet receive skip unrelated or malformed frames until its finite deadline instead of failing the whole receive operation.
- [ ] Add detailed send and receive error reporting to `PacketSocket`.

## AF_PACKET Transport

- [ ] Define a message-level transport interface that supports both streams and whole L2 frames.
- [ ] Keep AF_UNIX exact-byte framing behind its stream adapter.
- [ ] Add an AF_PACKET message adapter that carries the common wire header in Ethernet payloads.
- [ ] Enforce the per-frame fragment limit before send and before receive-side allocation.
- [ ] Add a veth-backed AF_PACKET integration test for complete NetGPU messages.
- [ ] Add a test for rejecting a wrong target device ID.
- [ ] Add a test for filtering frames addressed to another destination MAC.
- [ ] Obtain the local interface MAC from the selected interface instead of requiring duplicate manual configuration.
- [ ] Keep peer MAC and interface selection explicit in initial configuration.
- [ ] Add a finite shutdown path for packet receive loops.

## Discovery And Session

- [ ] Add a discovery request message with a protocol version.
- [ ] Add a discovery response containing a stable device ID and bounded name.
- [ ] Send discovery to the Ethernet broadcast MAC on the configured interface.
- [ ] Reply from the host to the requesting client's source MAC.
- [ ] Add a handshake that confirms protocol version, message limits and device identity.
- [ ] Reject unsupported protocol versions during handshake.
- [ ] Add session IDs and request IDs to the handshake exchange.
- [ ] Add a local test for discovery, handshake success and version mismatch.
- [ ] Document that initial discovery works only on the same L2 broadcast domain.

## Host Daemon Skeleton

- [ ] Add `netgpu-hostd` as a target in the root CMake file.
- [ ] Parse only the required initial options: interface and optional maximum message size.
- [ ] Start the packet socket and print a clear ready/error status.
- [ ] Add a receive loop that decodes Ethernet and wire headers before dispatch.
- [ ] Add a small `MessageType` dispatcher with one handler per supported message.
- [ ] Handle discovery and handshake with capability replies.
- [ ] Return an explicit unsupported-message reply for unimplemented request types.
- [ ] Add stub shader-module and command-buffer handlers that reject requests cleanly.
- [ ] Add signal-driven shutdown without a background worker pool.
- [ ] Add a hostd smoke test that does not require Vulkan.
- [ ] Run hostd on the physical host and exercise discovery from the Packard Bell.

## Host Vulkan Preflight And Context

- [ ] Record the host GPU model, PCI ID, kernel driver and Vulkan ICD.
- [ ] Verify `vulkaninfo --summary` on the host lists the intended GPU.
- [ ] Add a small host-only Vulkan probe that creates an instance and enumerates devices.
- [ ] Extend the probe to create one device and one compute queue.
- [ ] Make hostd select the native host driver explicitly, excluding the NetGPU ICD.
- [ ] Add a `VulkanDevice` owner with one clear create/destroy lifecycle.
- [ ] Submit an empty command buffer and wait for completion.
- [ ] Return a host capability message with conservative, verified limits.

## Client ICD Loader Skeleton

- [ ] Add `client/manifest/netgpu_icd.json.in` with a relative shared-library path.
- [ ] Add the ICD shared-library target to the root CMake file.
- [ ] Add the linker export map and keep standard Vulkan symbols hidden.
- [ ] Implement loader interface negotiation.
- [ ] Implement `vk_icdGetInstanceProcAddr` with only the required global entry points.
- [ ] Implement the minimum global enumeration functions needed by the loader.
- [ ] Implement instance creation and destruction with correct dispatch data.
- [ ] Implement physical-device enumeration returning no devices when no host is discovered.
- [ ] Add the physical-device procedure lookup required by the chosen loader interface version.
- [ ] Test discovery with `VK_DRIVER_FILES` pointing at the build manifest.
- [ ] Verify `vulkaninfo --summary` can load the ICD without affecting other installed drivers.
- [ ] Verify the ICD exports no unintended `vk*` symbols and does not link to `libvulkan`.

## First Remote Compute Slice

- [ ] Define the first supported Vulkan subset and advertise no unsupported extensions.
- [ ] Expose one remote physical device and one queue family from host capabilities.
- [ ] Add stable remote object IDs for device, queue, buffer and memory objects.
- [ ] Implement buffer creation and destruction on the client and host.
- [ ] Implement one host-visible memory allocation and bind path.
- [ ] Implement bounded upload and readback messages.
- [ ] Implement SPIR-V shader-module creation and destruction.
- [ ] Implement one descriptor-set layout and one storage-buffer descriptor.
- [ ] Implement one compute pipeline layout and compute pipeline.
- [ ] Record only bind-pipeline, bind-descriptor-set and dispatch commands.
- [ ] Submit one command buffer and return a completion response.
- [ ] Add a deterministic vector-add shader test and compare results with CPU output.
- [ ] Run vector-add over AF_UNIX locally.
- [ ] Run vector-add from the Packard Bell client to the physical host over AF_PACKET.

## Reliability And Resource Limits

- [ ] Bound aggregate in-flight reassembly memory independently of the 64 MiB message limit.
- [ ] Add per-client limits for live objects and outstanding requests.
- [ ] Add sequence checking and duplicate-request handling.
- [ ] Add acknowledgements and bounded retransmission for L2 requests.
- [ ] Add credits/backpressure for host receive and execution queues.
- [ ] Return device-lost or disconnected errors to outstanding client requests.
- [ ] Clean up remote objects when a client disconnects.
- [ ] Add malformed-header and malformed-payload tests for each handler.
- [ ] Add disconnect tests during header, payload, execution and response.
- [ ] Record packet drops, retransmits, queue depth and request latency.
- [ ] Measure before considering `PACKET_MMAP` or worker-thread scaling.

## Graphics And Presentation

- [ ] Define the smallest graphics feature/format set needed by `vkcube`.
- [ ] Implement a remote render target and one clear operation.
- [ ] Add the minimum graphics pipeline and draw-command subset.
- [ ] Implement remote image allocation and image readback.
- [ ] Add client-side presentation to the active Linux window system.
- [ ] Advertise surface and swapchain extensions only after their entry points work.
- [ ] Run `vkcube` with only the NetGPU ICD selected.
- [ ] Test resize, repeated presentation and clean window shutdown.

## Packaging And Release

- [ ] Add user-local ICD installation and removal targets.
- [ ] Add a least-privilege hostd service definition.
- [ ] Keep `CAP_NET_RAW` limited to the packet component or a small helper.
- [ ] Document interface, MTU, permissions, supported Vulkan subset and trusted-LAN assumptions.
- [ ] Audit third-party source files and add required notices.
- [ ] Record the physical host/client hardware, kernel, NIC and driver versions for each release test.
- [ ] Run the complete CTest suite and the AF_PACKET integration suite before tagging a release.
