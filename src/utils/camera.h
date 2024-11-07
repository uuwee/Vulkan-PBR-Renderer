
struct Camera {
	HMM_Vec3 pos;
	HMM_Quat ori;

	// for first-person controls
	float pitch;
	float yaw;

	HMM_Vec3 lazy_pos;
	HMM_Quat lazy_ori;

	float aspect_ratio;
	float z_near;
	float z_far;

	// The following are cached in `UpdateCamera`, do not set by hand
	HMM_Mat4 clip_from_world;
	HMM_Mat4 clip_from_view;
	HMM_Mat4 view_from_world;
	HMM_Mat4 view_from_clip;
	HMM_Mat4 world_from_view;
	HMM_Mat4 world_from_clip;

	inline HMM_Vec3 GetRight()   const { return world_from_view.Columns[0].XYZ; }

	// The world & camera coordinates are always right handed. It's either +Y is down +Z forward, or +Y up +Z back
	#ifdef CAMERA_VIEW_SPACE_IS_POSITIVE_Y_DOWN
	inline HMM_Vec3 GetUp()      const { return HMM_MulV3F(world_from_view.Columns[1].XYZ, -1.f); }
	inline HMM_Vec3 GetDown()    const { return world_from_view.Columns[1].XYZ; }
	inline HMM_Vec3 GetForward() const { return world_from_view.Columns[2].XYZ; }
	#else
	inline HMM_Vec3 GetUp()      const { return world_from_view.Columns[1].XYZ; }
	inline HMM_Vec3 GetDown()    const { return world_from_view.Columns[1].XYZ * -1.f; }
	inline HMM_Vec3 GetForward() const { return world_from_view.Columns[2].XYZ * -1.f; }
	#endif
};

static void UpdateCamera(Camera* camera, const Input::Frame& inputs, float movement_speed, float mouse_speed,
	float FOV_degrees, float aspect_ratio_x_over_y, float z_near, float z_far)
{
	if (camera->ori.X == 0 && camera->ori.Y == 0 && camera->ori.Z == 0 && camera->ori.W == 0) { // reset ori?

#ifdef CAMERA_VIEW_SPACE_IS_POSITIVE_Y_DOWN
		camera->ori = HMM_QFromAxisAngle_RH(HMM_V3(1, 0, 0), -HMM_PI32 / 2.f); // Rotate the camera to face +Y (zero rotation would be facing +Z)
#else
		camera->ori = HMM_QFromAxisAngle_RH(HMM_V3(1, 0, 0), HMM_PI32 / 2.f); // the HMM_PI32 / 2.f is to rotate the camera to face +Y (zero rotation would be facing -Z)
#endif
	}

	bool has_focus =
		inputs.KeyIsDown(Input::Key::MouseRight) ||
		inputs.KeyIsDown(Input::Key::LeftControl) ||
		inputs.KeyIsDown(Input::Key::RightControl);

	if (inputs.KeyIsDown(Input::Key::MouseRight)) {
		// we need to make rotators for the pitch delta and the yaw delta, and then just multiply the camera's orientation with it!
		camera->yaw   += -mouse_speed * (float)inputs.raw_mouse_input[0];
		camera->pitch += -mouse_speed * (float)inputs.raw_mouse_input[1];

		HMM_Quat pitch_rotator = HMM_QFromAxisAngle_RH(HMM_V3(1, 0, 0), camera->pitch - HMM_PI32 / 2.f);
		HMM_Quat yaw_rotator = HMM_QFromAxisAngle_RH(HMM_V3(0, 0, 1), camera->yaw);
		camera->ori = HMM_MulQ(yaw_rotator, pitch_rotator);
		camera->ori = HMM_NormQ(camera->ori);
	}

	if (has_focus) {
		if (inputs.KeyIsDown(Input::Key::Shift)) {
			movement_speed *= 3.f;
		}
		if (inputs.KeyIsDown(Input::Key::Control)) {
			movement_speed *= 0.1f;
		}
		if (inputs.KeyIsDown(Input::Key::W)) {
			camera->pos = camera->pos + camera->GetForward() * movement_speed;
		}
		if (inputs.KeyIsDown(Input::Key::S)) {
			camera->pos = camera->pos + camera->GetForward() * -movement_speed;
		}
		if (inputs.KeyIsDown(Input::Key::D)) {
			camera->pos = camera->pos + camera->GetRight() * movement_speed;
		}
		if (inputs.KeyIsDown(Input::Key::A)) {
			camera->pos = camera->pos + camera->GetRight() * -movement_speed;
		}
		if (inputs.KeyIsDown(Input::Key::E)) {
			camera->pos = camera->pos + HMM_V3(0, 0, movement_speed);
		}
		if (inputs.KeyIsDown(Input::Key::Q)) {
			camera->pos = camera->pos + HMM_V3(0, 0, -movement_speed);
		}
	}

	// Smoothly interpolate lazy position and ori
	camera->lazy_pos = HMM_LerpV3(camera->lazy_pos, 0.2f, camera->pos);
	camera->lazy_ori = HMM_SLerp(camera->lazy_ori, 0.2f, camera->ori);
	
	camera->aspect_ratio = aspect_ratio_x_over_y;
	camera->z_near = z_near;
	camera->z_far = z_far;

#if 1
	camera->world_from_view = HMM_MulM4(HMM_Translate(camera->lazy_pos), HMM_QToM4(camera->lazy_ori));
#else
	camera->world_from_view = HMM_MulM4(HMM_Translate(camera->pos), HMM_QToM4(camera->ori));
#endif
	camera->view_from_world = HMM_QToM4(HMM_InvQ(camera->lazy_ori)) * HMM_Translate(HMM_MulV3F(camera->lazy_pos, -1.f));
	
	// "_RH_ZO" means "right handed, zero-to-one NDC z range"
#ifdef CAMERA_VIEW_SPACE_IS_POSITIVE_Y_DOWN
	// Somewhat counter-intuitively, the HMM_Perspective_LH_ZO function is the true right-handed perspective function. RH_ZO requires a Z flip, RH_NO doesn't, and LH_ZO doesn't either.
	camera->clip_from_view = HMM_Perspective_LH_ZO(HMM_AngleDeg(FOV_degrees), camera->aspect_ratio, camera->z_near, camera->z_far);
#else
	camera->clip_from_view = HMM_Perspective_RH_ZO(HMM_AngleDeg(FOV_degrees), camera->aspect_ratio, camera->z_near, camera->z_far);
#endif

	camera->view_from_clip = HMM_InvGeneralM4(camera->clip_from_view);
	
	camera->clip_from_world = HMM_MulM4(camera->clip_from_view, camera->view_from_world);
	camera->world_from_clip = HMM_InvGeneralM4(camera->clip_from_world); //camera->view_to_world * camera->clip_to_view;
}