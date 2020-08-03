--[[
 Copyright 2020 Fujitsu

 author: Li Xiaoming <lixm.fnst@cn.fujitsu.com>

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
--]]


_AFT.testVerbStatusSuccess('testMedia_resultSuccess','mediascanner','media_result', {})

_AFT.testVerbStatusSuccess('testSubscribeAddSuccess','mediascanner','subscribe', {value="media_added"})
_AFT.testVerbStatusSuccess('testSubscribeRemoveSuccess','mediascanner','subscribe', {value="media_removed"})

_AFT.testVerbStatusSuccess('testUnsubscribeAddSuccess','mediascanner','unsubscribe', {value="media_added"})
_AFT.testVerbStatusSuccess('testUnsubscribeRemoveSuccess','mediascanner','unsubscribe', {value="media_removed"})