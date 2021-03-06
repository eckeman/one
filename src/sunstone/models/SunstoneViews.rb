# -------------------------------------------------------------------------- #
# Copyright 2002-2014, OpenNebula Project (OpenNebula.org), C12G Labs        #
#                                                                            #
# Licensed under the Apache License, Version 2.0 (the "License"); you may    #
# not use this file except in compliance with the License. You may obtain    #
# a copy of the License at                                                   #
#                                                                            #
# http://www.apache.org/licenses/LICENSE-2.0                                 #
#                                                                            #
# Unless required by applicable law or agreed to in writing, software        #
# distributed under the License is distributed on an "AS IS" BASIS,          #
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   #
# See the License for the specific language governing permissions and        #
# limitations under the License.                                             #
#--------------------------------------------------------------------------- #

require 'yaml'
require 'json'

require 'pp'

VIEWS_CONFIGURATION_FILE = ETC_LOCATION + "/sunstone-views.yaml"
VIEWS_CONFIGURATION_DIR = ETC_LOCATION + "/sunstone-views/"

class SunstoneViews
	def initialize
		@views_config = YAML.load_file(VIEWS_CONFIGURATION_FILE)

		base_path = SUNSTONE_ROOT_DIR+'/public/js/'

        @views = Hash.new

        Dir[VIEWS_CONFIGURATION_DIR+'*.yaml'].each do |p_path|
            m = p_path.match(/^#{VIEWS_CONFIGURATION_DIR}(.*).yaml$/)
            if m && m[1]
                @views[m[1]] = YAML.load_file(p_path)
            end
        end
	end

	def view(user_name, group_name, view_name=nil)
        available_views = available_views(user_name, group_name)

		if view_name && available_views.include?(view_name)
			return @views[view_name]
		else
			return @views[available_views.first]
		end
	end

    def available_views(user_name, group_name)
        user = OpenNebula::User.new_with_id(
                                OpenNebula::User::SELF,
                                $cloud_auth.client(user_name))
        user.info

        group = OpenNebula::Group.new_with_id(user.gid, $cloud_auth.client(user_name))
        group.info

        if group["TEMPLATE/SUNSTONE_VIEWS"]
            available_views = group["TEMPLATE/SUNSTONE_VIEWS"].split(",")
        else
            available_views = ['cloud']
        end

        return available_views
    end

    def available_tabs
        @views_config['available_tabs']
    end

    def logo
        @views_config['logo']
    end
end